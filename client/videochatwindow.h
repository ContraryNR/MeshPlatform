#ifndef VIDEOCHATWINDOW_H
#define VIDEOCHATWINDOW_H

#include <QDialog>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include "audioplayer.h"

class VideoChatWindow : public QDialog, public audioplayer
{
    Q_OBJECT

public:
    explicit VideoChatWindow(int asr=48000, int acc=1, QWidget* parent = nullptr);
    ~VideoChatWindow();
    void showLocalVideo(const QImage& frame);
    void showRemoteVideo(const QImage& frame, int peerHostNum);
    void setPeerName(const QString& name);
    void startDurationTimer();
    void setMuteState(bool muted);
    void setTopMuteState(bool muted);
signals:
    void hangUpClicked();
    void muteToggled(bool muted);
    void topMuteToggled(bool muted);
private slots:
    void onDurationTick();
    void onMuteButtonClicked();
    void onTopMuteButtonClicked();

private:
    QLabel* localVideoLabel;
    QLabel* remoteVideoLabel;
    QLabel* peerNameLabel;
    QLabel* durationLabel;
    QPushButton* muteButton;
    QPushButton* topMuteButton;
    QPushButton* hangUpButton;
    QTimer* durationTimer;
    QElapsedTimer elapsed;
    bool m_muted{false};
    bool m_topMuted{false};
    bool m_closing{false};

protected:
    void closeEvent(QCloseEvent* event) override;
};

#endif // VIDEOCHATWINDOW_H
