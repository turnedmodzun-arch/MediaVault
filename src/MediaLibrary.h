#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace mv {

struct MediaItem {
    QString title;      // display title
    QString path;       // absolute file path
    int season = 0;     // TV only
    int episode = 0;    // TV only
};

struct Season {
    int number = 0;
    QVector<MediaItem> episodes;
};

struct Show {
    QString name;
    QVector<Season> seasons;
};

struct Album {
    QString name;
    QVector<MediaItem> tracks;
};

struct Artist {
    QString name;
    QVector<Album> albums;
};

struct Library {
    QString root;
    QVector<MediaItem> movies;
    QVector<Show> shows;
    QVector<Artist> artists;
    QStringList warnings;   // e.g. a missing Music folder
};

// Scans <root>/Movies, <root>/TV Shows and <root>/Music (case-insensitive;
// "TV", "TV Shows", "TVShows" and "Series" are accepted for the TV folder).
Library scanLibrary(const QString &root);

bool isVideoFile(const QString &path);
bool isAudioFile(const QString &path);

// "The.Matrix.1999.1080p" -> "The Matrix 1999 1080p"; leading track numbers
// are handled separately by the music scanner.
QString prettifyName(const QString &baseName);

// Parses S01E02 / 1x02 from a file name. Returns false if not found.
bool parseEpisode(const QString &fileName, int &season, int &episode);

} // namespace mv
