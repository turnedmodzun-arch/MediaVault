# MediaVault

A cross-platform (Windows, macOS, Linux) media player for movies, TV shows and music, written in C++17 with Qt 6.

Point it at one folder and it builds a browsable library from the three folders inside:

```
Media/
├── Movies/
│   ├── The Matrix (1999).mkv
│   └── Alien (1979)/Alien (1979).mp4
├── TV Shows/
│   └── Breaking Bad/
│       └── Season 1/Breaking.Bad.S01E01.mkv
└── Music/
    └── Pink Floyd/
        └── The Wall/
            └── 01 - In The Flesh.flac
```

Folder names are matched case-insensitively. `TV`, `TVShows` and `Series` also work for the TV folder.

## Using it

- **File → Open Media Folder…** (Ctrl/Cmd+O) and pick the folder that *contains* Music, Movies and TV Shows. The choice is remembered.
- Or from a terminal: `MediaVault /path/to/Media`
- Double-click or press Enter on an item to play. Playback moves on to the next item automatically.
- Type in the filter box to narrow the tree.
- Keys: **Space** play/pause, **←/→** seek 10s, **F11** fullscreen, **Ctrl/Cmd+R** rescan.

### How files are recognised

| Type | Layout | Notes |
|------|--------|-------|
| Movies | any depth under `Movies/` | Title comes from the file name |
| TV | `TV Shows/<Show>/…/<episode>` | Season/episode from `S01E02` or `1x02`, otherwise from a `Season N` folder |
| Music | `Music/<Artist>/<Album>/<track>` | Leading track numbers are stripped; loose files go under "Unknown Artist" or "Singles" |

Formats depend on the Qt Multimedia backend (FFmpeg on Qt 6.5+), which covers mkv, mp4, avi, mov, webm, mp3, flac, ogg, m4a, opus, wav and more.

## Building

Requires CMake 3.21+, a C++17 compiler and Qt 6.5+ with the Multimedia module.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build        # scanner tests
./build/MediaVault /path/to/Media
```

Ubuntu/Debian: `sudo apt install qt6-base-dev qt6-multimedia-dev libqt6multimediawidgets6 cmake g++`

## GitHub Actions

`.github/workflows/build.yml` builds and tests on Linux, Windows and macOS for every push and pull request, and uploads the results as artifacts (Windows is bundled with `windeployqt`, macOS with `macdeployqt`). Pushing a tag such as `v0.1.0` also publishes a GitHub Release with those builds attached.

## Roadmap ideas

Metadata/artwork (TMDB, embedded tags), subtitle and audio track selection, watched/resume tracking, a music queue and shuffle, gapless playback.
