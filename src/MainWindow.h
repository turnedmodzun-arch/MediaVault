#pragma once

#include <QMainWindow>

#include "MediaLibrary.h"

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

private:
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
    bool m_wasMaximized = false;
};
