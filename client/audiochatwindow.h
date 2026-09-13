#ifndef AUDIOCHATWINDOW_H
#define AUDIOCHATWINDOW_H
#include <QDialog>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QVector>
#include "audioplayer.h"

class QPaintEvent;
class AudioLevelBar : public QWidget
{
    Q_OBJECT
public:
    explicit AudioLevelBar(QWidget* parent = nullptr);
    void pushLevel(int level);
    void resetLevels();
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    static constexpr int kBarCount = 60;
    //audioCapture中FrameDuration=20ms(未提供ui修改接口)
    //(sampleRate影响的是1s内的采样数量,修改sampleRate只能间接影响单个Frame内的采样点(uint_16)数量)
    //60个槽位/每个槽位对应一个Frame=>完整的槽位区间描述60*0.02=1.2s(换言之完整更新周期=1.2s)
    QVector<int> m_levels;
    int m_head{0};
};

class AudioChatWindow : public QDialog, public audioplayer
{
    Q_OBJECT
public:
    explicit AudioChatWindow(int sr=48000, int ch=1, QWidget* parent = nullptr);
    ~AudioChatWindow();
    void startDurationTimer();
    void setPeerName(const QString& name);
    void pushRemoteLevel(int level);
    void pushLocalLevel(int level);
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
    QLabel*     peerNameLabel;
    QLabel*     durationLabel;
    AudioLevelBar* remoteLevelBar;
    AudioLevelBar* localLevelBar;
    QPushButton* muteButton;
    QPushButton* topMuteButton;
    QPushButton* hangUpButton;
    QTimer*     durationTimer;
    QElapsedTimer elapsed;
    bool        m_muted{false};
    bool        m_topMuted{false};
    bool        m_closing{false};

protected:
    void closeEvent(QCloseEvent* event) override;
};

#endif // AUDIOCHATWINDOW_H
