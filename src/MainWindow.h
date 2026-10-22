#pragma once

#include <QMainWindow>

#include "MediaLibrary.h"

class QAction;
namespace mv { class DiscordPresence; }

class QAudioOutput;
class QLabel;
class QLineEdit;
class QMediaPlayer;
class QPushButton;
class QSlider;
class QTimer;
class QEvent;
class QTreeWidget;
class QTreeWidgetItem;
class QVideoWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Scans `root` (which should contain Music, Movies and TV Shows folders).
    void openLibrary(const QString &root);
    // Re-opens the folder used last time, if any.
    void openSavedLibrary();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void chooseFolder();
    void rescan();
    void onItemActivated(QTreeWidgetItem *item, int column);
    void togglePlay();
    void playNext();
    void playPrevious();
    void toggleFullscreen();
    void applyFilter(const QString &text);
    void onDiscordToggled(bool on);
    void promptDiscordClientId();

private:
    // Tells Discord what is playing. Pass a position when the player hasn't
    // caught up yet (e.g. right after a seek); -1 reads it from the player.
    void updatePresence(qint64 positionOverrideMs = -1);
    void seekTo(qint64 ms);
    void setFullscreenMode(bool on);
    void showControls();
    void populate();
    void playItem(QTreeWidgetItem *item);
    QList<QTreeWidgetItem *> leafItems() const;
    bool filterItem(QTreeWidgetItem *item, const QString &needle);
    static QString formatTime(qint64 ms);

    mv::Library m_library;
    QString m_root;

    QTreeWidget *m_tree = nullptr;
    QLineEdit *m_search = nullptr;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;
    QVideoWidget *m_video = nullptr;
    QLabel *m_nowPlaying = nullptr;
    QLabel *m_timeLabel = nullptr;
    QSlider *m_seek = nullptr;
    QSlider *m_volume = nullptr;
    QPushButton *m_playBtn = nullptr;
    QTreeWidgetItem *m_current = nullptr;
    bool m_seeking = false;

    QWidget *m_left = nullptr;          // library pane, hidden in fullscreen
    QWidget *m_controlsBar = nullptr;   // transport controls, auto-hide in fullscreen
    QTimer *m_hideTimer = nullptr;
    mv::DiscordPresence *m_discord = nullptr;
    QAction *m_discordToggle = nullptr;
    bool m_wasMaximized = false;
};
