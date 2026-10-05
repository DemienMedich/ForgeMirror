#pragma once
#include <QWidget>
#include <QDeadlineTimer>
#include <filesystem>
#include <functional>
#include <QString>

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QBoxLayout;
class QFormLayout;
class QResizeEvent;
class QtDisclosureButton;

class QtPomodoro : public QWidget {
public:
    explicit QtPomodoro(QWidget* parent = nullptr, std::filesystem::path storage = {},
        int workSeconds = 25 * 60, int breakSeconds = 5 * 60,
        int longBreakSeconds = 15 * 60, int cyclesBeforeLong = 4,
        std::filesystem::path assetRoot = {});
    void setRewardHandler(std::function<QString(int, std::int64_t)> handler) { rewardHandler_ = std::move(handler); }
    void setRewardStatusHandler(std::function<QString(int)> statusHandler,
        std::function<QString()> rulesTooltipHandler = {});
    void setAdministrator(bool administrator);
    void setQuickStateChanged(std::function<void()> handler);
    void setMotionPolicy(std::function<bool()> policy);
    QString quickSummary() const;
    QString quickToggleText() const;
    bool quickNextEnabled() const;
    void quickToggle();
    void quickNext();
    void quickReset();
    void advanceSecondsForTest(int seconds);
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    enum Phase { Work, Break, LongBreak };
    void startOrResume();
    void finishInterval(bool awardEligible = true);
    void reset();
    void refresh();
    void refreshRewardStatus();
    void refreshSoundInventory();
    void saveSettings();
    void playSound(Phase completed);
    int duration(Phase phase) const;
    QString phaseName(Phase phase) const;
    QTimer* timer_;
    QLabel* phaseLabel_;
    QLabel* timeLabel_;
    QLabel* statusLabel_;
    QLabel* rewardStatusLabel_;
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
    QTimer* rewardStatusTimer_ = nullptr;
    std::int64_t workStartedAt_ = 0;
    std::filesystem::path storage_;
    std::filesystem::path assetRoot_;
    std::function<QString(int, std::int64_t)> rewardHandler_;
    std::function<QString(int)> rewardStatusHandler_;
    std::function<QString()> rulesTooltipHandler_;
    QSpinBox* workMinutes_;
    QSpinBox* breakMinutes_;
    QSpinBox* longBreakMinutes_;
    QSpinBox* cyclesSetting_;
    QCheckBox* autoAdvanceSetting_;
    QWidget* soundSettings_;
    QLabel* soundDirectoryLabel_;
    QLabel* soundAvailabilityLabel_;
    QCheckBox* soundEnabled_;
    QComboBox* focusSound_;
    QComboBox* breakSound_;
    QSpinBox* soundVolume_;
    QFormLayout* soundForm_ = nullptr;
    QDeadlineTimer deadline_;
    std::function<void()> quickStateChanged_;
    QBoxLayout* rootLayout_ = nullptr;
    QtDisclosureButton* settingsToggle_ = nullptr;
    QLabel* settingsSummary_ = nullptr;
};
