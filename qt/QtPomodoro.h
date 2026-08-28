#pragma once
#include <QWidget>
#include <QDeadlineTimer>

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

class QtPomodoro : public QWidget {
public:
    explicit QtPomodoro(QWidget* parent = nullptr, int workSeconds = 25 * 60,
        int breakSeconds = 5 * 60, int longBreakSeconds = 15 * 60, int cyclesBeforeLong = 4);
    void advanceSecondsForTest(int seconds);
private:
    enum Phase { Work, Break, LongBreak };
    void startOrResume();
    void finishInterval();
    void reset();
    void refresh();
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
    QDeadlineTimer deadline_;
};
