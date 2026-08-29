#pragma once
#include <QWidget>
#include <QDeadlineTimer>
#include <filesystem>
#include <functional>

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
class QSpinBox;
class QCheckBox;

class QtPomodoro : public QWidget {
public:
    explicit QtPomodoro(QWidget* parent = nullptr, std::filesystem::path storage = {},
        int workSeconds = 25 * 60, int breakSeconds = 5 * 60,
        int longBreakSeconds = 15 * 60, int cyclesBeforeLong = 4);
    void setRewardHandler(std::function<QString(int, std::int64_t)> handler) { rewardHandler_ = std::move(handler); }
    void advanceSecondsForTest(int seconds);
private:
    enum Phase { Work, Break, LongBreak };
    void startOrResume();
    void finishInterval();
    void reset();
    void refresh();
    void saveSettings();
    int duration(Phase phase) const;
    QString phaseName(Phase phase) const;
    QTimer* timer_;
    QLabel* phaseLabel_;
    QLabel* timeLabel_;
    QLabel* statusLabel_;
    QLabel* cyclesLabel_;
    QProgressBar* progress_;
    QPushButton* start_;
    QPushButton* pause_;
    QPushButton* reset_;
    QPushButton* next_;
    Phase phase_ = Work;
    Phase nextPhase_ = Work;
    int remaining_ = 0;
    int cycles_ = 0;
    int workSeconds_;
    int breakSeconds_;
    int longBreakSeconds_;
    int cyclesBeforeLong_;
    bool running_ = false;
    bool awaiting_ = false;
    bool autoAdvance_ = false;
    std::int64_t workStartedAt_ = 0;
    std::filesystem::path storage_;
    std::function<QString(int, std::int64_t)> rewardHandler_;
    QSpinBox* workMinutes_;
    QSpinBox* breakMinutes_;
    QSpinBox* longBreakMinutes_;
    QSpinBox* cyclesSetting_;
    QCheckBox* autoAdvanceSetting_;
    QDeadlineTimer deadline_;
};
