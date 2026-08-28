#include "QtPomodoro.h"
#include <QtWidgets>
#include <algorithm>

QtPomodoro::QtPomodoro(QWidget* parent, int workSeconds, int breakSeconds,
    int longBreakSeconds, int cyclesBeforeLong) : QWidget(parent),
    workSeconds_(std::max(1, workSeconds)), breakSeconds_(std::max(1, breakSeconds)),
    longBreakSeconds_(std::max(1, longBreakSeconds)), cyclesBeforeLong_(std::max(1, cyclesBeforeLong)) {
    setObjectName("pomodoroPanel");
    auto* root = new QHBoxLayout(this); root->setContentsMargins(0, 0, 0, 0); root->setSpacing(16);
    auto* session = new QFrame; session->setProperty("metric", true);
    auto* sessionBox = new QVBoxLayout(session); sessionBox->setContentsMargins(16, 16, 16, 16); sessionBox->setSpacing(8);
    auto* heading = new QLabel(QString::fromUtf8("Сессия")); heading->setObjectName("pomodoroHeading"); sessionBox->addWidget(heading);
    phaseLabel_ = new QLabel; phaseLabel_->setObjectName("pomodoroPhase"); sessionBox->addWidget(phaseLabel_);
    timeLabel_ = new QLabel; timeLabel_->setObjectName("pomodoroTime"); timeLabel_->setProperty("timerValue", true); sessionBox->addWidget(timeLabel_);
    progress_ = new QProgressBar; progress_->setObjectName("pomodoroProgress"); progress_->setTextVisible(false); sessionBox->addWidget(progress_);
    cyclesLabel_ = new QLabel; cyclesLabel_->setObjectName("pomodoroCycles"); sessionBox->addWidget(cyclesLabel_);
    statusLabel_ = new QLabel; statusLabel_->setObjectName("pomodoroStatus"); statusLabel_->setWordWrap(true); sessionBox->addWidget(statusLabel_);
    sessionBox->addStretch(); root->addWidget(session, 3);
    auto* controls = new QFrame; controls->setProperty("metric", true);
    auto* controlBox = new QVBoxLayout(controls); controlBox->setContentsMargins(16, 16, 16, 16); controlBox->setSpacing(8);
    controlBox->addWidget(new QLabel(QString::fromUtf8("Управление")));
    start_ = new QPushButton(QString::fromUtf8("Старт фокуса")); start_->setObjectName("pomodoroStart"); start_->setProperty("primary", true); start_->setFixedHeight(32); controlBox->addWidget(start_);
    pause_ = new QPushButton(QString::fromUtf8("Пауза")); pause_->setObjectName("pomodoroPause"); controlBox->addWidget(pause_);
    next_ = new QPushButton(QString::fromUtf8("Начать следующий")); next_->setObjectName("pomodoroNext"); controlBox->addWidget(next_);
    reset_ = new QPushButton(QString::fromUtf8("Сбросить")); reset_->setObjectName("pomodoroReset"); controlBox->addWidget(reset_);
    auto* note = new QLabel(QString::fromUtf8("Локальный таймер. Монеты и постоянные настройки будут подключены отдельным этапом."));
    note->setWordWrap(true); controlBox->addWidget(note); controlBox->addStretch(); root->addWidget(controls, 2);
    timer_ = new QTimer(this); timer_->setInterval(250);
    connect(timer_, &QTimer::timeout, this, [this] {
        const auto milliseconds = deadline_.remainingTime();
        remaining_ = milliseconds <= 0 ? 0 : int((milliseconds + 999) / 1000);
        if (remaining_ == 0) finishInterval(); else refresh();
    });
    connect(start_, &QPushButton::clicked, this, [this] { startOrResume(); });
    connect(pause_, &QPushButton::clicked, this, [this] { running_ = false; timer_->stop(); refresh(); });
    connect(reset_, &QPushButton::clicked, this, [this] { reset(); });
    connect(next_, &QPushButton::clicked, this, [this] { if (awaiting_) { phase_ = nextPhase_; awaiting_ = false; remaining_ = duration(phase_); startOrResume(); } });
    reset();
}

int QtPomodoro::duration(Phase phase) const { return phase == Work ? workSeconds_ : phase == Break ? breakSeconds_ : longBreakSeconds_; }
QString QtPomodoro::phaseName(Phase phase) const {
    return QString::fromUtf8(phase == Work ? "Фокус" : phase == Break ? "Перерыв" : "Длинный перерыв");
}
void QtPomodoro::startOrResume() {
    if (awaiting_) return;
    if (remaining_ <= 0) remaining_ = duration(phase_);
    running_ = true; deadline_.setRemainingTime(qint64(remaining_) * 1000); timer_->start(); refresh();
}
void QtPomodoro::finishInterval() {
    timer_->stop(); running_ = false; remaining_ = 0; awaiting_ = true;
    if (phase_ == Work) {
        cycles_ = std::min(cycles_ + 1, cyclesBeforeLong_);
        nextPhase_ = cycles_ >= cyclesBeforeLong_ ? LongBreak : Break;
    } else {
        if (phase_ == LongBreak) cycles_ = 0;
        nextPhase_ = Work;
    }
    refresh();
}
void QtPomodoro::reset() {
    timer_->stop(); phase_ = Work; nextPhase_ = Work; remaining_ = workSeconds_;
    cycles_ = 0; running_ = false; awaiting_ = false; refresh();
}
void QtPomodoro::advanceSecondsForTest(int seconds) {
    if (!running_ || seconds <= 0) return;
    remaining_ = std::max(0, remaining_ - seconds);
    deadline_.setRemainingTime(qint64(remaining_) * 1000);
    if (remaining_ == 0) finishInterval(); else refresh();
}
void QtPomodoro::refresh() {
    const int total = duration(phase_);
    phaseLabel_->setText(phaseName(phase_));
    timeLabel_->setText(QString("%1:%2").arg(remaining_ / 60, 2, 10, QChar('0')).arg(remaining_ % 60, 2, 10, QChar('0')));
    progress_->setRange(0, total); progress_->setValue(total - remaining_);
    cyclesLabel_->setText(QString::fromUtf8("Фокусов: %1 / %2").arg(cycles_).arg(cyclesBeforeLong_));
    statusLabel_->setText(awaiting_ ? QString::fromUtf8("Завершено. Следующий интервал: ") + phaseName(nextPhase_) :
        running_ ? QString::fromUtf8("Таймер идёт") : remaining_ < total ? QString::fromUtf8("Пауза") : QString::fromUtf8("Готов к старту"));
    start_->setVisible(!running_ && !awaiting_); start_->setText(remaining_ < total ? QString::fromUtf8("Продолжить") : QString::fromUtf8("Старт фокуса"));
    pause_->setVisible(running_); next_->setVisible(awaiting_); reset_->setEnabled(running_ || awaiting_ || remaining_ < total);
}
