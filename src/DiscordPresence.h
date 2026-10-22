#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class QLocalSocket;
class QTimer;

namespace mv {

struct Activity {
    enum class Kind { None, Watching, Listening };

    Kind kind = Kind::None;   // None clears the presence
    QString details;          // first line, e.g. show or track name
    QString state;            // second line, e.g. "Season 1 · E02 – Title"
    bool playing = false;
    qint64 positionMs = 0;
    qint64 durationMs = 0;    // 0 = unknown
};

// --- Pure helpers (unit tested) -------------------------------------------

// Discord IPC frame: little-endian uint32 opcode, uint32 length, JSON body.
QByteArray encodeFrame(quint32 opcode, const QJsonObject &payload);
// Removes one complete frame from the front of `buffer`. Returns false if the
// buffer doesn't hold a complete frame yet.
bool decodeFrame(QByteArray &buffer, quint32 &opcode, QJsonObject &payload);

// Discord requires 2..128 characters for details/state.
QString clampText(const QString &text);

// Builds the "activity" object for SET_ACTIVITY. `nowMs` is the current Unix
// time in milliseconds (a parameter so it can be tested).
QJsonObject buildActivity(const Activity &activity, qint64 nowMs);

// --- Client ---------------------------------------------------------------

// Talks to the local Discord client over its IPC socket. Does nothing until
// enabled and given an Application ID (create one at
// https://discord.com/developers/applications). Reconnects automatically if
// Discord is started later.
class DiscordPresence : public QObject {
    Q_OBJECT
public:
    explicit DiscordPresence(QObject *parent = nullptr);
    ~DiscordPresence() override;

    void setClientId(const QString &clientId);
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    bool isReady() const { return m_ready; }

    // Calls are debounced; only the latest activity is sent.
    void setActivity(const Activity &activity);

signals:
    void statusChanged(const QString &text);

private:
    void connectToDiscord();
    void tryNextCandidate();
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onSocketError();
    void handleFrame(quint32 opcode, const QJsonObject &payload);
    void send(quint32 opcode, const QJsonObject &payload);
    void flush();
    void shutdownSocket();

    QLocalSocket *m_sock = nullptr;
    QTimer *m_flushTimer = nullptr;
    QTimer *m_retryTimer = nullptr;

    QString m_clientId;
    bool m_enabled = false;
    bool m_connecting = false;
    bool m_ready = false;

    QByteArray m_buffer;
    QStringList m_candidates;
    int m_candidateIdx = 0;

    Activity m_activity;
    qint64 m_activityStampMs = 0;
};

} // namespace mv
