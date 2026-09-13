#include "audiochatwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QDateTime>
#include <QCloseEvent>

AudioLevelBar::AudioLevelBar(QWidget* parent)
    : QWidget(parent)
{
    m_levels.fill(0, kBarCount);
    setMinimumHeight(48);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStyleSheet("background-color: #1a1a1a; border: 1px solid #333; border-radius: 4px;");
}

void AudioLevelBar::pushLevel(int level)
{
    if(level < 0) level = 0;
    if(level > 100) level = 100;
    m_levels[m_head] = level;
    m_head = (m_head + 1) % kBarCount;
    update();
}

void AudioLevelBar::resetLevels()
{
    m_levels.fill(0, kBarCount);
    m_head = 0;
    update();
}

void AudioLevelBar::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int w = width();
    const int h = height();
    int barWidth = w / kBarCount;
    if(barWidth < 1) barWidth = 1;
    int gap = (w - barWidth * kBarCount) / kBarCount;
    if(gap < 0) gap = 0;

    for(int i = 0; i < kBarCount; ++i)
    {
        int idx = (m_head + i) % kBarCount;
        int level = m_levels[idx];
        int barH = (level * (h - 4)) / 100;
        int x = i * (barWidth + gap);
        int y = h - barH - 2;

        QColor color;
        if(level < 40)      color = QColor(46, 204, 113);
        else if(level < 75) color = QColor(241, 196, 15);
        else                color = QColor(231, 76, 60);

        p.fillRect(x, y, barWidth, barH, color);
    }
}

AudioChatWindow::AudioChatWindow(int sr, int ch, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("音频通话");
    resize(360, 250);
    setFixedSize(360, 250);
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 10, 12, 10);
    mainLayout->setSpacing(8);

    peerNameLabel = new QLabel("正在与对方音频通话...", this);
    peerNameLabel->setAlignment(Qt::AlignCenter);
    QFont f = peerNameLabel->font();
    f.setPointSize(11);
    f.setBold(true);
    peerNameLabel->setFont(f);
    mainLayout->addWidget(peerNameLabel);

    durationLabel = new QLabel("00:00", this);
    durationLabel->setAlignment(Qt::AlignCenter);
    QFont df = durationLabel->font();
    df.setPointSize(10);
    durationLabel->setFont(df);
    durationLabel->setStyleSheet("color: #888;");
    mainLayout->addWidget(durationLabel);

    remoteLevelBar = new AudioLevelBar(this);
    QHBoxLayout* remoteBarRow = new QHBoxLayout;
    remoteBarRow->setSpacing(6);
    QLabel* remoteHint = new QLabel("对端声音", this);
    remoteHint->setStyleSheet("color: #aaa; font-size: 9pt;");
    remoteHint->setMinimumWidth(60);
    remoteBarRow->addWidget(remoteHint);
    remoteBarRow->addWidget(remoteLevelBar, 1);
    mainLayout->addLayout(remoteBarRow);

    localLevelBar = new AudioLevelBar(this);
    QHBoxLayout* localBarRow = new QHBoxLayout;
    localBarRow->setSpacing(6);
    QLabel* localHint = new QLabel("本端声音", this);
    localHint->setStyleSheet("color: #aaa; font-size: 9pt;");
    localHint->setMinimumWidth(60);
    localBarRow->addWidget(localHint);
    localBarRow->addWidget(localLevelBar, 1);
    mainLayout->addLayout(localBarRow);

    QHBoxLayout* btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);
    muteButton = new QPushButton("静音", this);
    muteButton->setCheckable(true);
    muteButton->setMinimumHeight(34);
    muteButton->setStyleSheet(
        "QPushButton { background-color: #34495e; color: white; font-size: 11pt; "
        "border: none; padding: 6px; border-radius: 4px; }"
        "QPushButton:checked { background-color: #e67e22; }");
    connect(muteButton, &QPushButton::toggled, this, &AudioChatWindow::onMuteButtonClicked);
    btnRow->addWidget(muteButton);

    topMuteButton = new QPushButton("关闭麦克风", this);
    topMuteButton->setCheckable(true);
    topMuteButton->setMinimumHeight(34);
    topMuteButton->setStyleSheet(
        "QPushButton { background-color: #2c3e50; color: white; font-size: 11pt; "
        "border: none; padding: 6px; border-radius: 4px; }"
        "QPushButton:checked { background-color: #c0392b; }");
    connect(topMuteButton, &QPushButton::toggled, this, &AudioChatWindow::onTopMuteButtonClicked);
    btnRow->addWidget(topMuteButton);

    hangUpButton = new QPushButton("结束通话", this);
    hangUpButton->setMinimumHeight(34);
    hangUpButton->setStyleSheet(
        "QPushButton { background-color: #e74c3c; color: white; font-size: 11pt; "
        "font-weight: bold; border: none; padding: 6px; border-radius: 4px; }");
    connect(hangUpButton, &QPushButton::clicked, this, &AudioChatWindow::hangUpClicked);
    btnRow->addWidget(hangUpButton);

    mainLayout->addLayout(btnRow);
    setLayout(mainLayout);

    durationTimer = new QTimer(this);
    durationTimer->setInterval(1000);
    connect(durationTimer, &QTimer::timeout, this, &AudioChatWindow::onDurationTick);
    elapsed.invalidate();

    initAudio(sr, ch);
}

AudioChatWindow::~AudioChatWindow()
{
    shutdownAudio();
}

void AudioChatWindow::closeEvent(QCloseEvent* event)
{
    if (!m_closing)
    {
        m_closing = true;
        emit hangUpClicked();
    }
    event->accept();
}

void AudioChatWindow::startDurationTimer()
{
    elapsed.start();
    durationLabel->setText("00:00");
    if(!durationTimer->isActive())
        durationTimer->start();
}

void AudioChatWindow::setPeerName(const QString& name)
{
    peerNameLabel->setText(QString("与 %1 音频通话中").arg(name));
}

void AudioChatWindow::pushRemoteLevel(int level)
{
    if(remoteLevelBar)
        remoteLevelBar->pushLevel(level);
}

void AudioChatWindow::pushLocalLevel(int level)
{
    if(localLevelBar)
        localLevelBar->pushLevel(level);
}

void AudioChatWindow::setMuteState(bool muted)
{
    if(muteButton->isChecked() == muted)
        return;
    muteButton->blockSignals(true);
    muteButton->setChecked(muted);
    muteButton->setText(muted ? "已静音" : "静音");
    muteButton->blockSignals(false);
    m_muted = muted;
}

void AudioChatWindow::setTopMuteState(bool muted)
{
    if(topMuteButton->isChecked() == muted)
        return;
    topMuteButton->blockSignals(true);
    topMuteButton->setChecked(muted);
    topMuteButton->setText(muted ? "麦克风已关" : "关闭麦克风");
    topMuteButton->blockSignals(false);
    m_topMuted = muted;
}

void AudioChatWindow::onDurationTick()
{
    if(!elapsed.isValid())
        return;
    qint64 ms = elapsed.elapsed();
    int totalSec = static_cast<int>(ms / 1000);
    int mm = totalSec / 60;
    int ss = totalSec % 60;
    durationLabel->setText(QString("%1:%2")
                               .arg(mm, 2, 10, QChar('0'))
                               .arg(ss, 2, 10, QChar('0')));
}

void AudioChatWindow::onMuteButtonClicked()
{
    m_muted = muteButton->isChecked();
    muteButton->setText(m_muted ? "已静音" : "静音");
    emit muteToggled(m_muted);
}

void AudioChatWindow::onTopMuteButtonClicked()
{
    m_topMuted = topMuteButton->isChecked();
    topMuteButton->setText(m_topMuted ? "麦克风已关" : "关闭麦克风");
    emit topMuteToggled(m_topMuted);
}
