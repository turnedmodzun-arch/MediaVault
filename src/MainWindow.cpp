#include "MainWindow.h"

#include <QAction>
#include <QAudioOutput>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

namespace {
constexpr int kPathRole = Qt::UserRole;
constexpr const char *kRootKey = "library/root";

QString episodeLabel(const mv::MediaItem &ep)
{
    if (ep.episode > 0)
        return QStringLiteral("E%1 – %2").arg(ep.episode, 2, 10, QLatin1Char('0')).arg(ep.title);
    return ep.title;
}

QTreeWidgetItem *makeLeaf(QTreeWidgetItem *parent, const QString &text, const QString &path)
{
    auto *it = new QTreeWidgetItem(parent, QStringList(text));
    it->setData(0, kPathRole, path);
    return it;
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("MediaVault");
    resize(1200, 720);

    // --- Library tree ---------------------------------------------------
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Filter…"));
    m_search->setClearButtonEnabled(true);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);

    m_left = new QWidget;
    auto *leftLayout = new QVBoxLayout(m_left);
    leftLayout->setContentsMargins(6, 6, 0, 6);
    leftLayout->addWidget(m_search);
    leftLayout->addWidget(m_tree);

    // --- Player ---------------------------------------------------------
    m_video = new QVideoWidget;
    m_video->setMinimumSize(320, 180);
    m_video->setStyleSheet("background:black;");

    m_nowPlaying = new QLabel(tr("Nothing playing"));
    m_nowPlaying->setAlignment(Qt::AlignCenter);

    auto makeBtn = [this](QStyle::StandardPixmap icon, const QString &tip) {
        auto *b = new QPushButton(style()->standardIcon(icon), QString());
        b->setToolTip(tip);
        b->setFlat(true);
        return b;
    };
    auto *prevBtn = makeBtn(QStyle::SP_MediaSkipBackward, tr("Previous"));
    m_playBtn = makeBtn(QStyle::SP_MediaPlay, tr("Play / Pause (Space)"));
    auto *stopBtn = makeBtn(QStyle::SP_MediaStop, tr("Stop"));
    auto *nextBtn = makeBtn(QStyle::SP_MediaSkipForward, tr("Next"));
    auto *fullBtn = makeBtn(QStyle::SP_TitleBarMaxButton, tr("Fullscreen (F11)"));

    m_seek = new QSlider(Qt::Horizontal);
    m_seek->setRange(0, 0);
    m_timeLabel = new QLabel("0:00 / 0:00");
    m_volume = new QSlider(Qt::Horizontal);
    m_volume->setRange(0, 100);
    m_volume->setValue(80);
    m_volume->setMaximumWidth(110);
    m_volume->setToolTip(tr("Volume"));

    m_controlsBar = new QWidget;
    auto *controls = new QHBoxLayout(m_controlsBar);
    controls->setContentsMargins(0, 0, 0, 0);
    controls->addWidget(prevBtn);
    controls->addWidget(m_playBtn);
    controls->addWidget(stopBtn);
    controls->addWidget(nextBtn);
    controls->addWidget(m_seek, 1);
    controls->addWidget(m_timeLabel);
    controls->addWidget(m_volume);
    controls->addWidget(fullBtn);

    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 6, 6, 6);
    rightLayout->addWidget(m_video, 1);
    rightLayout->addWidget(m_nowPlaying);
    rightLayout->addWidget(m_controlsBar);

    auto *splitter = new QSplitter;
    splitter->addWidget(m_left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({340, 860});
    setCentralWidget(splitter);

    // --- Playback engine -----------------------------------------------
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_audio->setVolume(0.8f);
    m_player->setAudioOutput(m_audio);
    m_player->setVideoOutput(m_video);

    // --- Menu -----------------------------------------------------------
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *openAct = fileMenu->addAction(tr("&Open Media Folder…"), QKeySequence::Open, this, &MainWindow::chooseFolder);
    Q_UNUSED(openAct);
    fileMenu->addAction(tr("&Rescan Library"), QKeySequence::Refresh, this, &MainWindow::rescan);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    // --- Wiring ---------------------------------------------------------
    connect(m_tree, &QTreeWidget::itemActivated, this, &MainWindow::onItemActivated);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &MainWindow::onItemActivated);
    connect(m_search, &QLineEdit::textChanged, this, &MainWindow::applyFilter);

    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::togglePlay);
    connect(stopBtn, &QPushButton::clicked, m_player, &QMediaPlayer::stop);
    connect(nextBtn, &QPushButton::clicked, this, &MainWindow::playNext);
    connect(prevBtn, &QPushButton::clicked, this, &MainWindow::playPrevious);
    connect(fullBtn, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);

    connect(m_volume, &QSlider::valueChanged, this, [this](int v) { m_audio->setVolume(v / 100.0f); });

    connect(m_seek, &QSlider::sliderPressed, this, [this] { m_seeking = true; });
    connect(m_seek, &QSlider::sliderReleased, this, [this] {
        m_seeking = false;
        m_player->setPosition(m_seek->value());
    });

    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 d) {
        m_seek->setRange(0, static_cast<int>(d));
        m_timeLabel->setText(formatTime(m_player->position()) + " / " + formatTime(d));
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 p) {
        if (!m_seeking)
            m_seek->setValue(static_cast<int>(p));
        m_timeLabel->setText(formatTime(p) + " / " + formatTime(m_player->duration()));
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
        m_playBtn->setIcon(style()->standardIcon(
            s == QMediaPlayer::PlayingState ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
        if (s == QMediaPlayer::EndOfMedia)
            playNext();   // auto-advance through the list
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &msg) {
                statusBar()->showMessage(tr("Playback error: %1").arg(msg), 8000);
            });

    // Fullscreen: the whole window goes fullscreen; controls auto-hide and
    // reappear on mouse movement. Esc / F11 / double-click on the video exit.
    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(3000);
    connect(m_hideTimer, &QTimer::timeout, this, [this] {
        if (isFullScreen() && !m_controlsBar->underMouse()) {
            m_controlsBar->hide();
            m_video->setCursor(Qt::BlankCursor);
        } else if (isFullScreen()) {
            m_hideTimer->start();
        }
    });
    m_video->setMouseTracking(true);
    m_video->installEventFilter(this);
    new QShortcut(Qt::Key_Escape, this, this, [this] {
        if (isFullScreen())
            setFullscreenMode(false);
    });

    new QShortcut(Qt::Key_Space, this, this, &MainWindow::togglePlay);
    new QShortcut(Qt::Key_F11, this, this, &MainWindow::toggleFullscreen);
    new QShortcut(Qt::Key_Right, this, this, [this] { m_player->setPosition(m_player->position() + 10000); });
    new QShortcut(Qt::Key_Left, this, this, [this] { m_player->setPosition(qMax<qint64>(0, m_player->position() - 10000)); });
}

void MainWindow::openSavedLibrary()
{
    const QString saved = QSettings().value(kRootKey).toString();
    if (!saved.isEmpty())
        openLibrary(saved);
    else
        statusBar()->showMessage(tr("Choose your media folder with File > Open Media Folder…"));
}

void MainWindow::chooseFolder()
{
    const QString start = m_root.isEmpty() ? QDir::homePath() : m_root;
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select media folder (containing Music, Movies and TV Shows)"), start);
    if (!dir.isEmpty())
        openLibrary(dir);
}

void MainWindow::rescan()
{
    if (!m_root.isEmpty())
        openLibrary(m_root);
}

void MainWindow::openLibrary(const QString &root)
{
    m_root = root;
    QSettings().setValue(kRootKey, root);

    m_player->stop();
    m_current = nullptr;
    m_library = mv::scanLibrary(root);
    populate();

    setWindowTitle(QStringLiteral("MediaVault – %1").arg(root));
    QString msg = tr("%1 movies, %2 shows, %3 artists")
                      .arg(m_library.movies.size())
                      .arg(m_library.shows.size())
                      .arg(m_library.artists.size());
    if (!m_library.warnings.isEmpty())
        msg += QStringLiteral("  |  ") + m_library.warnings.join(QStringLiteral("; "));
    statusBar()->showMessage(msg);
}

void MainWindow::populate()
{
    m_tree->clear();

    auto *movies = new QTreeWidgetItem(m_tree, QStringList(tr("Movies (%1)").arg(m_library.movies.size())));
    for (const auto &m : m_library.movies)
        makeLeaf(movies, m.title, m.path);

    auto *tv = new QTreeWidgetItem(m_tree, QStringList(tr("TV Shows (%1)").arg(m_library.shows.size())));
    for (const auto &show : m_library.shows) {
        auto *showItem = new QTreeWidgetItem(tv, QStringList(show.name));
        for (const auto &season : show.seasons) {
            const QString label = season.number > 0 ? tr("Season %1").arg(season.number) : tr("Episodes");
            auto *seasonItem = new QTreeWidgetItem(showItem, QStringList(label));
            for (const auto &ep : season.episodes)
                makeLeaf(seasonItem, episodeLabel(ep), ep.path);
        }
    }

    auto *music = new QTreeWidgetItem(m_tree, QStringList(tr("Music (%1)").arg(m_library.artists.size())));
    for (const auto &artist : m_library.artists) {
        auto *artistItem = new QTreeWidgetItem(music, QStringList(artist.name));
        for (const auto &album : artist.albums) {
            auto *albumItem = new QTreeWidgetItem(artistItem, QStringList(album.name));
            for (const auto &t : album.tracks)
                makeLeaf(albumItem, t.title, t.path);
        }
    }

    m_tree->expandToDepth(0);
    applyFilter(m_search->text());
}

QList<QTreeWidgetItem *> MainWindow::leafItems() const
{
    QList<QTreeWidgetItem *> out;
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if ((*it)->data(0, kPathRole).isValid())
            out << *it;
        ++it;
    }
    return out;
}

void MainWindow::onItemActivated(QTreeWidgetItem *item, int)
{
    if (item && item->data(0, kPathRole).isValid())
        playItem(item);
}

void MainWindow::playItem(QTreeWidgetItem *item)
{
    const QString path = item->data(0, kPathRole).toString();
    m_current = item;
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->play();

    // Breadcrumb: "Show > Season 1 > E02 – Title"
    QStringList crumbs;
    for (QTreeWidgetItem *p = item; p && p->parent(); p = p->parent())
        crumbs.prepend(p->text(0));
    m_nowPlaying->setText(crumbs.join(QStringLiteral("  ›  ")));
}

void MainWindow::togglePlay()
{
    switch (m_player->playbackState()) {
    case QMediaPlayer::PlayingState:
        m_player->pause();
        break;
    case QMediaPlayer::PausedState:
        m_player->play();
        break;
    case QMediaPlayer::StoppedState:
        if (m_current) {
            m_player->play();
        } else if (auto *sel = m_tree->currentItem(); sel && sel->data(0, kPathRole).isValid()) {
            playItem(sel);
        }
        break;
    }
}

void MainWindow::playNext()
{
    const auto leaves = leafItems();
    const int i = leaves.indexOf(m_current);
    // Skip items hidden by the filter.
    for (int j = i + 1; i >= 0 && j < leaves.size(); ++j) {
        if (!leaves[j]->isHidden()) {
            m_tree->setCurrentItem(leaves[j]);
            playItem(leaves[j]);
            return;
        }
    }
}

void MainWindow::playPrevious()
{
    // Restart the current track if we're more than 3 seconds in.
    if (m_player->position() > 3000) {
        m_player->setPosition(0);
        return;
    }
    const auto leaves = leafItems();
    const int i = leaves.indexOf(m_current);
    for (int j = i - 1; i > 0 && j >= 0; --j) {
        if (!leaves[j]->isHidden()) {
            m_tree->setCurrentItem(leaves[j]);
            playItem(leaves[j]);
            return;
        }
    }
}

void MainWindow::toggleFullscreen()
{
    setFullscreenMode(!isFullScreen());
}

void MainWindow::setFullscreenMode(bool on)
{
    if (on == isFullScreen())
        return;

    if (on) {
        m_wasMaximized = isMaximized();
        m_left->hide();
        m_nowPlaying->hide();
        menuBar()->hide();
        statusBar()->hide();
        showFullScreen();
        showControls();   // visible first, then auto-hides after a few seconds
    } else {
        m_hideTimer->stop();
        m_video->unsetCursor();
        m_left->show();
        m_nowPlaying->show();
        m_controlsBar->show();
        menuBar()->show();
        statusBar()->show();
        if (m_wasMaximized)
            showMaximized();
        else
            showNormal();
    }
}

void MainWindow::showControls()
{
    m_controlsBar->show();
    m_video->unsetCursor();
    if (isFullScreen())
        m_hideTimer->start();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_video) {
        switch (event->type()) {
        case QEvent::MouseMove:
            if (isFullScreen())
                showControls();
            break;
        case QEvent::MouseButtonDblClick:
            toggleFullscreen();
            return true;
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

bool MainWindow::filterItem(QTreeWidgetItem *item, const QString &needle)
{
    const bool selfMatch = needle.isEmpty() || item->text(0).contains(needle, Qt::CaseInsensitive);
    bool childVisible = false;
    for (int i = 0; i < item->childCount(); ++i) {
        // If the parent itself matches, keep its whole subtree visible.
        QTreeWidgetItem *c = item->child(i);
        const bool v = selfMatch ? (filterItem(c, QString()), true) : filterItem(c, needle);
        childVisible = childVisible || v;
    }
    const bool visible = selfMatch || childVisible;
    item->setHidden(!visible);
    return visible;
}

void MainWindow::applyFilter(const QString &text)
{
    const QString needle = text.trimmed();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = m_tree->topLevelItem(i);
        // Category rows always stay visible; only filter below them.
        for (int j = 0; j < top->childCount(); ++j)
            filterItem(top->child(j), needle);
        top->setHidden(false);
        if (!needle.isEmpty())
            top->setExpanded(true);
    }
}

QString MainWindow::formatTime(qint64 ms)
{
    const qint64 total = ms / 1000;
    const qint64 h = total / 3600, m = (total / 60) % 60, s = total % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}
