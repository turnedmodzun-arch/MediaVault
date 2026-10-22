#include <QtTest>

#include "DiscordPresence.h"

class TestDiscord : public QObject {
    Q_OBJECT

private slots:
    void frameRoundTrip()
    {
        QJsonObject o;
        o["v"] = 1;
        o["client_id"] = "123";
        QByteArray buf = mv::encodeFrame(0, o);

        // Header: opcode 0, length little-endian.
        QCOMPARE(static_cast<quint8>(buf[0]), quint8(0));
        QCOMPARE(static_cast<quint8>(buf[4]) | (static_cast<quint8>(buf[5]) << 8),
                 buf.size() - 8);

        quint32 op = 99;
        QJsonObject out;
        QVERIFY(mv::decodeFrame(buf, op, out));
        QCOMPARE(op, quint32(0));
        QCOMPARE(out["client_id"].toString(), QString("123"));
        QVERIFY(buf.isEmpty());
    }

    void partialAndMultipleFrames()
    {
        const QByteArray a = mv::encodeFrame(1, QJsonObject{{"evt", "READY"}});
        const QByteArray b = mv::encodeFrame(3, QJsonObject{{"n", 1}});

        QByteArray buf = a.left(5);
        quint32 op = 0;
        QJsonObject out;
        QVERIFY(!mv::decodeFrame(buf, op, out));          // header incomplete
        buf = a.left(a.size() - 1);
        QVERIFY(!mv::decodeFrame(buf, op, out));          // body incomplete
        QCOMPARE(buf.size(), a.size() - 1);                // left untouched

        buf = a + b;
        QVERIFY(mv::decodeFrame(buf, op, out));
        QCOMPARE(op, quint32(1));
        QVERIFY(mv::decodeFrame(buf, op, out));
        QCOMPARE(op, quint32(3));
        QVERIFY(buf.isEmpty());
    }

    void clampText()
    {
        QCOMPARE(mv::clampText(QString(300, 'a')).size(), 128);
        QVERIFY(mv::clampText("x").size() >= 2);
        QCOMPARE(mv::clampText("  hello   world "), QString("hello world"));
    }

    void playingActivityHasTimestamps()
    {
        mv::Activity a;
        a.kind = mv::Activity::Kind::Watching;
        a.details = "Breaking Bad";
        a.state = "Season 1 · E02";
        a.playing = true;
        a.positionMs = 60'000;
        a.durationMs = 2'700'000;

        const qint64 now = 1'000'000'000'000;   // ms
        const QJsonObject o = mv::buildActivity(a, now);
        QCOMPARE(o["type"].toInt(), 3);
        QCOMPARE(o["details"].toString(), QString("Breaking Bad"));
        const QJsonObject ts = o["timestamps"].toObject();
        QCOMPARE(ts["start"].toInteger(), qint64(1'000'000'000 - 60));
        QCOMPARE(ts["end"].toInteger(), qint64(1'000'000'000 - 60 + 2700));
    }

    void listeningTypeAndUnknownDuration()
    {
        mv::Activity a;
        a.kind = mv::Activity::Kind::Listening;
        a.details = "In The Flesh";
        a.playing = true;
        const QJsonObject o = mv::buildActivity(a, 5'000'000);
        QCOMPARE(o["type"].toInt(), 2);
        QVERIFY(o["timestamps"].toObject().contains("start"));
        QVERIFY(!o["timestamps"].toObject().contains("end"));
        QVERIFY(!o.contains("state"));
    }

    void pausedHasNoTimestamps()
    {
        mv::Activity a;
        a.kind = mv::Activity::Kind::Watching;
        a.details = "Alien (1979)";
        a.state = "Movie";
        a.playing = false;
        const QJsonObject o = mv::buildActivity(a, 5'000'000);
        QVERIFY(!o.contains("timestamps"));
        QVERIFY(o["state"].toString().contains("Paused"));
    }
};

QTEST_APPLESS_MAIN(TestDiscord)
#include "tst_discord.moc"
