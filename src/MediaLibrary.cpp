#include "MediaLibrary.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace mv {

namespace {

const QSet<QString> kVideoExt = {"mkv", "mp4", "m4v", "avi", "mov", "wmv", "webm",
                                 "mpg", "mpeg", "ts", "m2ts", "flv", "ogv"};
const QSet<QString> kAudioExt = {"mp3", "flac", "wav", "ogg", "oga", "m4a", "aac",
                                 "opus", "wma", "aiff", "ape"};

// Finds a direct child directory whose name matches one of `names`, ignoring case.
QString findChildDir(const QDir &root, const QStringList &names)
{
    const QStringList dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &d : dirs) {
        for (const QString &n : names) {
            if (d.compare(n, Qt::CaseInsensitive) == 0)
                return root.absoluteFilePath(d);
        }
    }
    return {};
}

QCollator makeCollator()
{
    QCollator c;
    c.setNumericMode(true);
    c.setCaseSensitivity(Qt::CaseInsensitive);
    return c;
}

QStringList filesUnder(const QString &dir, bool (*pred)(const QString &))
{
    QStringList out;
    QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        if (pred(p))
            out << p;
    }
    return out;
}

int seasonFromFolder(const QString &folder)
{
    static const QRegularExpression re(
        QStringLiteral("^(?:season\\s*|s)(\\d{1,2})$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(folder.trimmed());
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

void scanMovies(const QString &dir, Library &lib)
{
    for (const QString &p : filesUnder(dir, isVideoFile)) {
        MediaItem it;
        it.path = p;
        it.title = prettifyName(QFileInfo(p).completeBaseName());
        lib.movies.push_back(it);
    }
    const QCollator c = makeCollator();
    std::sort(lib.movies.begin(), lib.movies.end(),
              [&](const MediaItem &a, const MediaItem &b) { return c.compare(a.title, b.title) < 0; });
}

void scanTv(const QString &dir, Library &lib)
{
    const QCollator c = makeCollator();
    const QDir root(dir);
    const QStringList showDirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &showName : showDirs) {
        const QString showPath = root.absoluteFilePath(showName);
        QMap<int, QVector<MediaItem>> bySeason;

        for (const QString &p : filesUnder(showPath, isVideoFile)) {
            const QFileInfo fi(p);
            MediaItem it;
            it.path = p;
            it.title = prettifyName(fi.completeBaseName());

            int s = 0, e = 0;
            if (parseEpisode(fi.fileName(), s, e)) {
                it.season = s;
                it.episode = e;
            } else {
                // Fall back to the nearest "Season N" folder above the file.
                QDir d = fi.dir();
                while (d.absolutePath() != showPath && d.absolutePath().startsWith(showPath)) {
                    const int fs = seasonFromFolder(d.dirName());
                    if (fs >= 0) { it.season = fs; break; }
                    if (!d.cdUp()) break;
                }
            }
            bySeason[it.season].push_back(it);
        }

        if (bySeason.isEmpty())
            continue;

        Show show;
        show.name = prettifyName(showName);
        for (auto sIt = bySeason.begin(); sIt != bySeason.end(); ++sIt) {
            Season season;
            season.number = sIt.key();
            season.episodes = sIt.value();
            std::sort(season.episodes.begin(), season.episodes.end(),
                      [&](const MediaItem &a, const MediaItem &b) {
                          if (a.episode != b.episode) return a.episode < b.episode;
                          return c.compare(a.title, b.title) < 0;
                      });
            show.seasons.push_back(season);
        }
        lib.shows.push_back(show);
    }
    std::sort(lib.shows.begin(), lib.shows.end(),
              [&](const Show &a, const Show &b) { return c.compare(a.name, b.name) < 0; });
}

QString stripTrackNumber(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^\\d{1,3}\\s*[-._)]?\\s+"));
    QString out = name;
    out.remove(re);
    return out.isEmpty() ? name : out;
}

void scanMusic(const QString &dir, Library &lib)
{
    const QCollator c = makeCollator();
    const QDir root(dir);

    // artist -> album -> tracks
    QMap<QString, QMap<QString, QVector<MediaItem>>> tree;

    for (const QString &p : filesUnder(dir, isAudioFile)) {
        const QStringList parts = root.relativeFilePath(p).split('/', Qt::SkipEmptyParts);
        QString artist = QStringLiteral("Unknown Artist");
        QString album = QStringLiteral("Singles");
        if (parts.size() >= 3) {
            artist = parts[0];
            album = parts[1];
        } else if (parts.size() == 2) {
            artist = parts[0];
        }

        MediaItem it;
        it.path = p;
        it.title = prettifyName(stripTrackNumber(QFileInfo(p).completeBaseName()));
        tree[artist][album].push_back(it);
    }

    for (auto aIt = tree.begin(); aIt != tree.end(); ++aIt) {
        Artist artist;
        artist.name = aIt.key();
        for (auto bIt = aIt.value().begin(); bIt != aIt.value().end(); ++bIt) {
            Album album;
            album.name = bIt.key();
            album.tracks = bIt.value();
            // File names usually start with the track number, so sort on the path.
            std::sort(album.tracks.begin(), album.tracks.end(),
                      [&](const MediaItem &a, const MediaItem &b) { return c.compare(a.path, b.path) < 0; });
            artist.albums.push_back(album);
        }
        std::sort(artist.albums.begin(), artist.albums.end(),
                  [&](const Album &a, const Album &b) { return c.compare(a.name, b.name) < 0; });
        lib.artists.push_back(artist);
    }
    std::sort(lib.artists.begin(), lib.artists.end(),
              [&](const Artist &a, const Artist &b) { return c.compare(a.name, b.name) < 0; });
}

} // namespace

bool isVideoFile(const QString &path)
{
    return kVideoExt.contains(QFileInfo(path).suffix().toLower());
}

bool isAudioFile(const QString &path)
{
    return kAudioExt.contains(QFileInfo(path).suffix().toLower());
}

QString prettifyName(const QString &baseName)
{
    QString s = baseName;
    s.replace('.', ' ');
    s.replace('_', ' ');
    return s.simplified();
}

bool parseEpisode(const QString &fileName, int &season, int &episode)
{
    static const QRegularExpression sxe(QStringLiteral("[Ss](\\d{1,2})[Ee](\\d{1,3})"));
    static const QRegularExpression nxn(QStringLiteral("(?<!\\d)(\\d{1,2})x(\\d{2,3})(?!\\d)"));

    auto m = sxe.match(fileName);
    if (!m.hasMatch())
        m = nxn.match(fileName);
    if (!m.hasMatch())
        return false;
    season = m.captured(1).toInt();
    episode = m.captured(2).toInt();
    return true;
}

Library scanLibrary(const QString &rootPath)
{
    Library lib;
    lib.root = rootPath;

    const QDir root(rootPath);
    if (!root.exists()) {
        lib.warnings << QStringLiteral("Folder does not exist: %1").arg(rootPath);
        return lib;
    }

    const QString movies = findChildDir(root, {"Movies", "Movie", "Films"});
    const QString tv = findChildDir(root, {"TV Shows", "TV", "TVShows", "TV-Shows", "Series"});
    const QString music = findChildDir(root, {"Music"});

    if (movies.isEmpty()) lib.warnings << QStringLiteral("No Movies folder found");
    else scanMovies(movies, lib);

    if (tv.isEmpty()) lib.warnings << QStringLiteral("No TV Shows folder found");
    else scanTv(tv, lib);

    if (music.isEmpty()) lib.warnings << QStringLiteral("No Music folder found");
    else scanMusic(music, lib);

    return lib;
}

} // namespace mv
