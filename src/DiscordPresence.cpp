#include "DiscordPresence.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTimer>
#include <QUuid>
#include <QtEndian>

namespace mv {

namespace {

enum Opcode : quint32 { OpHandshake = 0, OpFrame = 1, OpClose = 2, OpPing = 3, OpPong = 4 };

constexpr quint32 kMaxFrame = 1u << 20;

// Where Discord's IPC socket may live. On Windows it is a named pipe, which
// QLocalSocket addresses by bare name.
QStringList ipcCandidates()
{
    QStringList out;
#ifdef Q_OS_WIN
    for (int i = 0; i < 10; ++i)
        out << QStringLiteral("discord-ipc-%1").arg(i);
#else
    QStringList dirs;
    for (const char *var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        const QString v = qEnvironmentVariable(var);
        if (!v.isEmpty() && !dirs.contains(v))
            dirs << v;
    }
    if (!dirs.contains(QStringLiteral("/tmp")))
        dirs << QStringLiteral("/tmp");

    // Plain installs, Flatpak and Snap all put the socket in different places.
    const QStringList subdirs = {QString(), QStringLiteral("/app/com.discordapp.Discord"),
                                 QStringLiteral("/snap.discord")};
    for (int i = 0; i < 10; ++i) {
        for (const QString &d : dirs) {
            for (const QString &sub : subdirs) {
                const QString p = QStringLiteral("%1%2/discord-ipc-%3").arg(QDir::cleanPath(d), sub).arg(i);
                if (QFileInfo::exists(p))
                    out << p;
            }
        }
    }
#endif
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

QByteArray encodeFrame(quint32 opcode, const QJsonObject &payload)
{
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray out(8, 0);
    qToLittleEndian<quint32>(opcode, out.data());
    qToLittleEndian<quint32>(static_cast<quint32>(body.size()), out.data() + 4);
    out += body;
    return out;
}

bool decodeFrame(QByteArray &buffer, quint32 &opcode, QJsonObject &payload)
{
    if (buffer.size() < 8)
        return false;
    opcode = qFromLittleEndian<quint32>(buffer.constData());
    const quint32 len = qFromLittleEndian<quint32>(buffer.constData() + 4);
    if (len > kMaxFrame) {   // corrupt stream; drop it rather than wait forever
        buffer.clear();
        return false;
    }
    if (static_cast<quint32>(buffer.size()) < 8 + len)
        return false;
    payload = QJsonDocument::fromJson(buffer.mid(8, static_cast<int>(len))).object();
    buffer.remove(0, 8 + static_cast<int>(len));
    return true;
}

QString clampText(const QString &text)
{
    QString s = text.simplified();
    if (s.size() > 128)
        s = s.left(127) + QChar(0x2026);
    while (s.size() < 2)
        s += QChar(0x00A0);
    return s;
}

QJsonObject buildActivity(const Activity &a, qint64 nowMs)
{
    QJsonObject act;
    // 2 = Listening, 3 = Watching
    act["type"] = a.kind == Activity::Kind::Listening ? 2 : 3;
    act["details"] = clampText(a.details);

    if (a.playing) {
        if (!a.state.isEmpty())
            act["state"] = clampText(a.state);
        // Discord expects Unix seconds.
        const qint64 start = (nowMs - a.positionMs) / 1000;
        QJsonObject ts;
        ts["start"] = start;
        if (a.durationMs > 0)
            ts["end"] = start + a.durationMs / 1000;
        act["timestamps"] = ts;
    } else {
        act["state"] = clampText(a.state.isEmpty() ? QStringLiteral("Paused")
                                                   : a.state + QStringLiteral(" (Paused)"));
    }
    return act;
}

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

DiscordPresence::DiscordPresence(QObject *parent)
    : QObject(parent)
{
    m_sock = new QLocalSocket(this);
    connect(m_sock, &QLocalSocket::connected, this, &DiscordPresence::onConnected);
    connect(m_sock, &QLocalSocket::readyRead, this, &DiscordPresence::onReadyRead);
    connect(m_sock, &QLocalSocket::disconnected, this, &DiscordPresence::onDisconnected);
    connect(m_sock, &QLocalSocket::errorOccurred, this, &DiscordPresence::onSocketError);

    // Discord rate-limits presence updates, so coalesce bursts (seek, skip).
    m_flushTimer = new QTimer(this);
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(800);
    connect(m_flushTimer, &QTimer::timeout, this, &DiscordPresence::flush);

    m_retryTimer = new QTimer(this);
    m_retryTimer->setSingleShot(true);
    m_retryTimer->setInterval(15000);
    connect(m_retryTimer, &QTimer::timeout, this, &DiscordPresence::connectToDiscord);
}

DiscordPresence::~DiscordPresence()
{
    shutdownSocket();
}

void DiscordPresence::setClientId(const QString &clientId)
{
    const QString id = clientId.trimmed();
    if (id == m_clientId)
        return;
    m_clientId = id;
    if (m_enabled) {
        shutdownSocket();
        connectToDiscord();
    }
}

void DiscordPresence::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    if (enabled) {
        if (m_clientId.isEmpty())
            emit statusChanged(tr("Discord: set an Application ID first"));
        connectToDiscord();
    } else {
        shutdownSocket();
        emit statusChanged(tr("Discord presence off"));
    }
}

void DiscordPresence::setActivity(const Activity &activity)
{
    m_activity = activity;
    m_activityStampMs = QDateTime::currentMSecsSinceEpoch();
    if (m_enabled)
        m_flushTimer->start();
}

void DiscordPresence::connectToDiscord()
{
    if (!m_enabled || m_clientId.isEmpty())
        return;
    if (m_sock->state() != QLocalSocket::UnconnectedState)
        return;
    m_candidates = ipcCandidates();
    m_candidateIdx = 0;
    tryNextCandidate();
}

void DiscordPresence::tryNextCandidate()
{
    if (!m_enabled)
        return;
    if (m_candidateIdx >= m_candidates.size()) {
        m_connecting = false;
        emit statusChanged(tr("Discord not running – will keep trying"));
        m_retryTimer->start();
        return;
    }
    m_connecting = true;
    m_sock->connectToServer(m_candidates.at(m_candidateIdx++));
}

void DiscordPresence::onConnected()
{
    m_connecting = false;
    m_retryTimer->stop();
    m_buffer.clear();

    QJsonObject hello;
    hello["v"] = 1;
    hello["client_id"] = m_clientId;
    send(OpHandshake, hello);
}

void DiscordPresence::onSocketError()
{
    // Errors while still looking for the socket mean "try the next path";
    // later errors are followed by disconnected(), which handles retrying.
    if (m_connecting) {
        m_connecting = false;
        QTimer::singleShot(0, this, &DiscordPresence::tryNextCandidate);
    }
}

void DiscordPresence::onDisconnected()
{
    m_ready = false;
    m_buffer.clear();
    if (m_enabled) {
        emit statusChanged(tr("Discord disconnected – will keep trying"));
        m_retryTimer->start();
    }
}

void DiscordPresence::onReadyRead()
{
    m_buffer += m_sock->readAll();
    quint32 op = 0;
    QJsonObject payload;
    while (decodeFrame(m_buffer, op, payload))
        handleFrame(op, payload);
}

void DiscordPresence::handleFrame(quint32 opcode, const QJsonObject &payload)
{
    switch (opcode) {
    case OpPing:
        send(OpPong, payload);
        break;
    case OpClose:
        emit statusChanged(tr("Discord closed the connection: %1")
                               .arg(payload.value("message").toString()));
        m_sock->abort();
        break;
    case OpFrame: {
        const QString evt = payload.value("evt").toString();
        if (evt == QLatin1String("READY")) {
            m_ready = true;
            const QString user = payload.value("data").toObject().value("user").toObject()
                                     .value("username").toString();
            emit statusChanged(tr("Discord presence connected (%1)").arg(user));
            flush();
        } else if (evt == QLatin1String("ERROR")) {
            emit statusChanged(tr("Discord error: %1")
                                   .arg(payload.value("data").toObject().value("message").toString()));
        }
        break;
    }
    default:
        break;
    }
}

void DiscordPresence::send(quint32 opcode, const QJsonObject &payload)
{
    if (m_sock->state() == QLocalSocket::ConnectedState)
        m_sock->write(encodeFrame(opcode, payload));
}

void DiscordPresence::flush()
{
    if (!m_ready)
        return;

    QJsonObject args;
    args["pid"] = static_cast<qint64>(QCoreApplication::applicationPid());

    if (m_activity.kind == Activity::Kind::None) {
        args["activity"] = QJsonValue::Null;   // clears the presence
    } else {
        Activity a = m_activity;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (a.playing)   // account for the debounce delay
            a.positionMs += now - m_activityStampMs;
        args["activity"] = buildActivity(a, now);
    }

    QJsonObject msg;
    msg["cmd"] = QStringLiteral("SET_ACTIVITY");
    msg["args"] = args;
    msg["nonce"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    send(OpFrame, msg);
}

void DiscordPresence::shutdownSocket()
{
    m_retryTimer->stop();
    m_flushTimer->stop();
    m_connecting = false;

    if (m_ready) {
        // Remove our presence before leaving.
        Activity none;
        const Activity saved = m_activity;
        m_activity = none;
        flush();
        m_activity = saved;
        m_sock->flush();
        m_sock->waitForBytesWritten(200);
    }
    m_ready = false;
    if (m_sock->state() != QLocalSocket::UnconnectedState)
        m_sock->abort();
}

} // namespace mv
