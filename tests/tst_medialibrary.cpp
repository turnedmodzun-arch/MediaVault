#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "MediaLibrary.h"

class TestMediaLibrary : public QObject {
    Q_OBJECT

    static void touch(const QString &path)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

private slots:
    void parseEpisodeFormats()
    {
        int s = 0, e = 0;
        QVERIFY(mv::parseEpisode("Show.Name.S02E10.1080p.mkv", s, e));
        QCOMPARE(s, 2);
        QCOMPARE(e, 10);
        QVERIFY(mv::parseEpisode("show 3x07.mp4", s, e));
        QCOMPARE(s, 3);
        QCOMPARE(e, 7);
        QVERIFY(!mv::parseEpisode("Movie (1999).mkv", s, e));
        // A resolution like 1920x1080 must not be read as an episode.
        QVERIFY(!mv::parseEpisode("clip 1920x1080.mkv", s, e));
    }

    void prettify()
    {
        QCOMPARE(mv::prettifyName("The.Matrix_1999"), QString("The Matrix 1999"));
    }

    void scansAllThreeFolders()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString r = tmp.path();

        touch(r + "/Movies/The.Matrix.1999.mkv");
        touch(r + "/Movies/Alien (1979)/Alien (1979).mp4");
        touch(r + "/Movies/readme.txt");

        touch(r + "/TV Shows/Breaking Bad/Season 1/Breaking.Bad.S01E02.mkv");
        touch(r + "/TV Shows/Breaking Bad/Season 1/Breaking.Bad.S01E01.mkv");
        touch(r + "/TV Shows/Firefly/Season 1/Pilot.mkv");  // season only from folder

        touch(r + "/Music/Pink Floyd/The Wall/02 - Another Brick.flac");
        touch(r + "/Music/Pink Floyd/The Wall/01 - In The Flesh.flac");
        touch(r + "/Music/Loose Track.mp3");

        const mv::Library lib = mv::scanLibrary(r);
        QVERIFY2(lib.warnings.isEmpty(), qPrintable(lib.warnings.join(", ")));

        QCOMPARE(lib.movies.size(), 2);
        QCOMPARE(lib.movies[0].title, QString("Alien (1979)"));

        QCOMPARE(lib.shows.size(), 2);
        QCOMPARE(lib.shows[0].name, QString("Breaking Bad"));
        QCOMPARE(lib.shows[0].seasons.size(), 1);
        QCOMPARE(lib.shows[0].seasons[0].episodes.size(), 2);
        QCOMPARE(lib.shows[0].seasons[0].episodes[0].episode, 1);
        QCOMPARE(lib.shows[1].seasons[0].number, 1);

        QCOMPARE(lib.artists.size(), 2);
        const auto &pf = lib.artists[1].name == "Pink Floyd" ? lib.artists[1] : lib.artists[0];
        QCOMPARE(pf.albums.size(), 1);
        QCOMPARE(pf.albums[0].tracks.size(), 2);
        QCOMPARE(pf.albums[0].tracks[0].title, QString("In The Flesh"));
    }

    void missingFoldersWarn()
    {
        QTemporaryDir tmp;
        const mv::Library lib = mv::scanLibrary(tmp.path());
        QCOMPARE(lib.warnings.size(), 3);
    }
};

QTEST_APPLESS_MAIN(TestMediaLibrary)
#include "tst_medialibrary.moc"
