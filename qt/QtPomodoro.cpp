#include "QtPomodoro.h"
#include <QtWidgets>
#include <QSaveFile>
#include <algorithm>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#endif

namespace {
QString settingsPath(const std::filesystem::path& storage) {
    return QString::fromUtf8((storage / "meta/ui.ini").u8string());
}
QMap<QString, QString> readPomodoro(const std::filesystem::path& storage) {
    QFile file(settingsPath(storage)); if (!file.open(QIODevice::ReadOnly)) return {};
    auto bytes = file.readAll(); if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
    QMap<QString, QString> values; bool section = false;
    for (const auto& raw : QString::fromUtf8(bytes).split('\n')) {
        const auto line = raw.trimmed();
        if (line.startsWith('[')) { section = line == "[pomodoro]"; continue; }
        if (!section) continue;
        const auto at = line.indexOf('='); if (at > 0) values[line.left(at).trimmed()] = line.mid(at + 1).trimmed();
    }
    return values;
}
bool writePomodoro(const std::filesystem::path& storage, const QMap<QString, QString>& values) {
    const auto path = settingsPath(storage); QFile input(path); QByteArray original;
    if (input.open(QIODevice::ReadOnly)) original = input.readAll();
    input.close();
    bool bom = original.startsWith("\xEF\xBB\xBF"); if (bom) original.remove(0, 3);
    auto lines = QString::fromUtf8(original).split('\n');
    int begin = -1, end = lines.size();
    for (int i = 0; i < lines.size(); ++i) {
        const auto line = lines[i].trimmed();
        if (line == "[pomodoro]") { begin = i; continue; }
        if (begin >= 0 && i > begin && line.startsWith('[')) { end = i; break; }
    }
    if (begin < 0) { if (!lines.isEmpty() && !lines.back().isEmpty()) lines << ""; begin = lines.size(); lines << "[pomodoro]"; end = lines.size(); }
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        int found = -1;
        for (int i = begin + 1; i < end; ++i) if (lines[i].section('=', 0, 0).trimmed() == it.key()) { found = i; break; }
        const auto replacement = it.key() + "=" + it.value();
        if (found >= 0) lines[found] = replacement;
        else { lines.insert(end++, replacement); }
    }
    const auto bytes = (bom ? QByteArray("\xEF\xBB\xBF") : QByteArray()) + lines.join('\n').toUtf8();
    QDir().mkpath(QFileInfo(path).absolutePath()); QSaveFile output(path); output.setDirectWriteFallback(false);
    return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
}
int integer(const QMap<QString, QString>& values, const QString& key, int fallback, int low, int high) {
    bool ok = false; const int value = values.value(key).toInt(&ok); return std::clamp(ok ? value : fallback, low, high);
}
QString bundledMusicDirectory(const std::filesystem::path& assetRoot) {
    QDir search(QString::fromUtf8(assetRoot.u8string()));
    if (!search.exists()) return {};
    for (int depth = 0; depth < 6; ++depth) {
        const QFileInfo candidate(search.filePath(QStringLiteral("data/music")));
        if (candidate.exists() && candidate.isDir() && !candidate.isSymLink())
            return candidate.absoluteFilePath();
        if (!search.cdUp()) break;
    }
    return {};
}
QString workspaceMusicDirectory(const std::filesystem::path& storage) {
    const QFileInfo candidate(QString::fromUtf8((storage / "music").u8string()));
    return candidate.exists() && candidate.isDir() && !candidate.isSymLink()
        ? candidate.absoluteFilePath() : QString();
}
QString selectedMusicDirectory(const std::filesystem::path& storage, const std::filesystem::path& assetRoot) {
    const auto local = workspaceMusicDirectory(storage);
    return !local.isEmpty() ? local : bundledMusicDirectory(assetRoot);
}
bool isSafeSoundFile(const QFileInfo& file);
bool containsUsableSounds(const QString& directoryPath) {
    if (directoryPath.isEmpty()) return false;
    const QDir directory(directoryPath);
    for (const auto& name : directory.entryList(QDir::Files | QDir::NoSymLinks, QDir::Name))
        if (isSafeSoundFile(QFileInfo(directory.filePath(name)))) return true;
    return false;
}
bool isSafeSoundFile(const QFileInfo& file) {
    if (file.isSymLink() || !file.isFile() || file.size() > 20 * 1024 * 1024) return false;
    const auto extension = file.suffix();
    return !extension.compare("wav", Qt::CaseInsensitive) || !extension.compare("mp3", Qt::CaseInsensitive);
}
QString soundPath(const std::filesystem::path& storage, const std::filesystem::path& assetRoot, const QString& relative) {
    if (relative.isEmpty()) return {};
    const QString prefix = "music/";
    if (!relative.startsWith(prefix)) return {};
    const auto name = relative.mid(prefix.size());
    if (name.isEmpty() || name.contains('/') || name.contains('\\') || name.contains(':') || name.contains('"')) return {};
    const auto localDirectory = workspaceMusicDirectory(storage);
    const auto bundledDirectory = bundledMusicDirectory(assetRoot);
    for (const auto& directory : {localDirectory, bundledDirectory}) {
        if (directory.isEmpty()) continue;
        const QFileInfo file(QDir(directory).filePath(name));
        if (isSafeSoundFile(file)) return file.absoluteFilePath();
    }
    return {};
}
void fillSounds(QComboBox* combo, const std::filesystem::path& storage,
    const std::filesystem::path& assetRoot, const QString& current) {
    combo->addItem(QString::fromUtf8("Системный сигнал"), QString());
    const auto path = selectedMusicDirectory(storage, assetRoot);
    if (!path.isEmpty()) {
        const QDir directory(path);
        for (const auto& name : directory.entryList(QDir::Files | QDir::NoSymLinks, QDir::Name)) {
            if (!isSafeSoundFile(QFileInfo(directory.filePath(name)))) continue;
            const auto relative = "music/" + name;
            if (!soundPath(storage, assetRoot, relative).isEmpty()) combo->addItem(name, relative);
        }
    }
    if (current.startsWith("music/") && combo->findData(current) < 0)
        combo->addItem(QString::fromUtf8("Недоступен: ") + QFileInfo(current).fileName(), current);
    combo->setCurrentIndex(std::max(0, combo->findData(current)));
}
}

QtPomodoro::QtPomodoro(QWidget* parent, std::filesystem::path storage, int workSeconds, int breakSeconds,
    int longBreakSeconds, int cyclesBeforeLong, std::filesystem::path assetRoot)
    : QWidget(parent), storage_(std::move(storage)),
    assetRoot_(assetRoot.empty() ? std::filesystem::u8path(QCoreApplication::applicationDirPath().toUtf8().toStdString()) : std::move(assetRoot)),
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
    const auto saved = readPomodoro(storage_);
    if (!storage_.empty()) {
        if (saved.contains("workMinutes")) workSeconds_ = integer(saved, "workMinutes", 25, 5, 120) * 60;
        if (saved.contains("breakMinutes")) breakSeconds_ = integer(saved, "breakMinutes", 5, 3, 60) * 60;
        if (saved.contains("longBreakMinutes")) longBreakSeconds_ = integer(saved, "longBreakMinutes", 15, 5, 90) * 60;
        if (saved.contains("cyclesBeforeLong")) cyclesBeforeLong_ = integer(saved, "cyclesBeforeLong", 4, 1, 8);
        if (saved.contains("autoAdvance")) autoAdvance_ = saved.value("autoAdvance") == "1";
    }
    auto* form = new QFormLayout; form->setSpacing(8);
    workMinutes_ = new QSpinBox; workMinutes_->setObjectName("pomodoroWorkMinutes"); workMinutes_->setRange(5, 120); workMinutes_->setValue(workSeconds_ / 60);
    breakMinutes_ = new QSpinBox; breakMinutes_->setObjectName("pomodoroBreakMinutes"); breakMinutes_->setRange(3, 60); breakMinutes_->setValue(breakSeconds_ / 60);
    longBreakMinutes_ = new QSpinBox; longBreakMinutes_->setObjectName("pomodoroLongBreakMinutes"); longBreakMinutes_->setRange(5, 90); longBreakMinutes_->setValue(longBreakSeconds_ / 60);
    cyclesSetting_ = new QSpinBox; cyclesSetting_->setObjectName("pomodoroCyclesSetting"); cyclesSetting_->setRange(1, 8); cyclesSetting_->setValue(cyclesBeforeLong_);
    autoAdvanceSetting_ = new QCheckBox(QString::fromUtf8("Автоматически начинать следующий")); autoAdvanceSetting_->setObjectName("pomodoroAutoAdvance"); autoAdvanceSetting_->setChecked(autoAdvance_);
    form->addRow(QString::fromUtf8("Фокус, мин"), workMinutes_); form->addRow(QString::fromUtf8("Перерыв, мин"), breakMinutes_);
    form->addRow(QString::fromUtf8("Длинный, мин"), longBreakMinutes_); form->addRow(QString::fromUtf8("Фокусов"), cyclesSetting_); form->addRow(autoAdvanceSetting_);
    controlBox->addLayout(form);
    auto* save = new QPushButton(QString::fromUtf8("Сохранить настройки")); save->setObjectName("pomodoroSaveSettings"); controlBox->addWidget(save);
    soundSettings_ = new QWidget;
    auto* soundForm = new QFormLayout(soundSettings_); soundForm->setContentsMargins(0, 0, 0, 0); soundForm->setSpacing(8);
    soundEnabled_ = new QCheckBox(QString::fromUtf8("Звук окончания")); soundEnabled_->setObjectName("pomodoroSoundEnabled");
    soundEnabled_->setChecked(!saved.contains("soundEnabled") || saved.value("soundEnabled") == "1");
    focusSound_ = new QComboBox; focusSound_->setObjectName("pomodoroFocusSound");
    breakSound_ = new QComboBox; breakSound_->setObjectName("pomodoroBreakSound");
    fillSounds(focusSound_, storage_, assetRoot_, saved.value("soundFocus"));
    fillSounds(breakSound_, storage_, assetRoot_, saved.value("soundBreak"));
    soundVolume_ = new QSpinBox; soundVolume_->setObjectName("pomodoroSoundVolume"); soundVolume_->setRange(0, 100); soundVolume_->setSuffix(" %");
    soundVolume_->setValue(integer(saved, "soundVolume", 80, 0, 100));
    soundForm->addRow(soundEnabled_); soundForm->addRow(QString::fromUtf8("После фокуса"), focusSound_);
    soundForm->addRow(QString::fromUtf8("После перерыва"), breakSound_); soundForm->addRow(QString::fromUtf8("Громкость"), soundVolume_);
    soundDirectoryLabel_ = new QLabel;
    soundDirectoryLabel_->setObjectName("pomodoroSoundDirectory");
    soundDirectoryLabel_->setWordWrap(true);
    soundForm->addRow(soundDirectoryLabel_);
    soundAvailabilityLabel_ = new QLabel(QString::fromUtf8("Файлы .wav/.mp3 не найдены."));
    soundAvailabilityLabel_->setObjectName("pomodoroSoundAvailability");
    soundAvailabilityLabel_->setWordWrap(true);
    soundForm->addRow(soundAvailabilityLabel_);
    auto* refreshSounds = new QPushButton(QString::fromUtf8("Обновить список звуков"));
    refreshSounds->setObjectName("pomodoroRefreshSounds");
    refreshSounds->setAccessibleName(QString::fromUtf8("Обновить список звуков Pomodoro"));
    soundForm->addRow(QString(), refreshSounds);
    refreshSoundInventory();
    soundSettings_->setToolTip(QString::fromUtf8("Администраторские сигналы из папки music рабочего места или поставки data/music."));
    controlBox->addWidget(soundSettings_); soundSettings_->hide();
    auto* note = new QLabel(QString::fromUtf8("Награда возможна только за полный фокус при личном входе и по правилам хранилища."));
    note->setWordWrap(true); controlBox->addWidget(note);
    rewardStatusLabel_ = new QLabel;
    rewardStatusLabel_->setObjectName("pomodoroRewardStatus");
    rewardStatusLabel_->setAccessibleName(QString::fromUtf8("Статус награды Pomodoro"));
    rewardStatusLabel_->setWordWrap(true);
    controlBox->addWidget(rewardStatusLabel_);
    controlBox->addStretch(); root->addWidget(controls, 2);
    timer_ = new QTimer(this); timer_->setInterval(250);
    connect(timer_, &QTimer::timeout, this, [this] {
        const auto milliseconds = deadline_.remainingTime();
        remaining_ = milliseconds <= 0 ? 0 : int((milliseconds + 999) / 1000);
        if (remaining_ == 0) finishInterval(); else refresh();
    });
    connect(start_, &QPushButton::clicked, this, [this] { startOrResume(); });
    connect(pause_, &QPushButton::clicked, this, [this] { running_ = false; timer_->stop(); refresh(); });
    connect(reset_, &QPushButton::clicked, this, [this] { reset(); });
    connect(save, &QPushButton::clicked, this, [this] { saveSettings(); });
    connect(workMinutes_, qOverload<int>(&QSpinBox::valueChanged), this, [this] { refreshRewardStatus(); });
    connect(refreshSounds, &QPushButton::clicked, this, [this] {
        refreshSoundInventory();
        statusLabel_->setProperty("rewardMessage", QString::fromUtf8("Список звуков обновлён."));
        refresh();
    });
    connect(next_, &QPushButton::clicked, this, [this] { if (awaiting_) { phase_ = nextPhase_; awaiting_ = false; remaining_ = duration(phase_); startOrResume(); } });
    rewardStatusTimer_ = new QTimer(this);
    rewardStatusTimer_->setInterval(60000);
    connect(rewardStatusTimer_, &QTimer::timeout, this, [this] { refreshRewardStatus(); });
    rewardStatusTimer_->start();
    reset();
}

int QtPomodoro::duration(Phase phase) const { return phase == Work ? workSeconds_ : phase == Break ? breakSeconds_ : longBreakSeconds_; }
QString QtPomodoro::phaseName(Phase phase) const {
    return QString::fromUtf8(phase == Work ? "Фокус" : phase == Break ? "Перерыв" : "Длинный перерыв");
}
void QtPomodoro::startOrResume() {
    if (awaiting_) return;
    statusLabel_->setProperty("rewardMessage", {});
    if (remaining_ <= 0) remaining_ = duration(phase_);
    if (phase_ == Work && remaining_ == duration(Work)) workStartedAt_ = QDateTime::currentSecsSinceEpoch();
    running_ = true; deadline_.setRemainingTime(qint64(remaining_) * 1000); timer_->start(); refresh();
}
void QtPomodoro::finishInterval(bool awardEligible) {
    timer_->stop(); running_ = false; remaining_ = 0; awaiting_ = true;
    const auto completedPhase = phase_;
    if (phase_ == Work) {
        if (awardEligible && rewardHandler_) {
            const auto result = rewardHandler_(workSeconds_ / 60, workStartedAt_);
            if (!result.isEmpty()) statusLabel_->setProperty("rewardMessage", result);
        }
        workStartedAt_ = 0;
        cycles_ = std::min(cycles_ + 1, cyclesBeforeLong_);
        nextPhase_ = cycles_ >= cyclesBeforeLong_ ? LongBreak : Break;
    } else {
        if (phase_ == LongBreak) cycles_ = 0;
        nextPhase_ = Work;
    }
    playSound(completedPhase);
    if (autoAdvance_) { phase_ = nextPhase_; awaiting_ = false; remaining_ = duration(phase_); startOrResume(); return; }
    refresh();
}
void QtPomodoro::reset() {
    timer_->stop(); phase_ = Work; nextPhase_ = Work; remaining_ = workSeconds_;
    cycles_ = 0; running_ = false; awaiting_ = false; workStartedAt_ = 0; statusLabel_->setProperty("rewardMessage", {}); refresh();
}
void QtPomodoro::saveSettings() {
    const QMap<QString, QString> values = {{"workMinutes", QString::number(workMinutes_->value())},
        {"breakMinutes", QString::number(breakMinutes_->value())}, {"longBreakMinutes", QString::number(longBreakMinutes_->value())},
        {"cyclesBeforeLong", QString::number(cyclesSetting_->value())}, {"autoAdvance", autoAdvanceSetting_->isChecked() ? "1" : "0"},
        {"soundEnabled", soundEnabled_->isChecked() ? "1" : "0"}, {"soundFocus", focusSound_->currentData().toString()},
        {"soundBreak", breakSound_->currentData().toString()}, {"soundVolume", QString::number(soundVolume_->value())}};
    if (!writePomodoro(storage_, values)) { statusLabel_->setText(QString::fromUtf8("Не удалось сохранить настройки.")); return; }
    workSeconds_ = workMinutes_->value() * 60; breakSeconds_ = breakMinutes_->value() * 60;
    longBreakSeconds_ = longBreakMinutes_->value() * 60; cyclesBeforeLong_ = cyclesSetting_->value(); autoAdvance_ = autoAdvanceSetting_->isChecked();
    cycles_ = std::min(cycles_, cyclesBeforeLong_);
    if (!running_ && !awaiting_) remaining_ = duration(phase_);
    statusLabel_->setProperty("rewardMessage", QString::fromUtf8("Настройки сохранены.")); refresh();
}
void QtPomodoro::setAdministrator(bool administrator) {
    soundSettings_->setVisible(administrator);
    refreshRewardStatus();
}
void QtPomodoro::setRewardStatusHandler(std::function<QString(int)> statusHandler,
    std::function<QString()> rulesTooltipHandler) {
    rewardStatusHandler_ = std::move(statusHandler);
    rulesTooltipHandler_ = std::move(rulesTooltipHandler);
    refreshRewardStatus();
}
void QtPomodoro::refreshRewardStatus() {
    if (!rewardStatusLabel_) return;
    const auto status = rewardStatusHandler_
        ? rewardStatusHandler_(workMinutes_ ? workMinutes_->value() : workSeconds_ / 60)
        : QString::fromUtf8("правила не загружены");
    rewardStatusLabel_->setText(QString::fromUtf8("Монеты: ") + status);
    rewardStatusLabel_->setToolTip(rulesTooltipHandler_ ? rulesTooltipHandler_() : QString());
}
void QtPomodoro::refreshSoundInventory() {
    const auto focus = focusSound_->currentData().toString();
    const auto pause = breakSound_->currentData().toString();
    focusSound_->clear(); breakSound_->clear();
    fillSounds(focusSound_, storage_, assetRoot_, focus);
    fillSounds(breakSound_, storage_, assetRoot_, pause);
    const auto directory = selectedMusicDirectory(storage_, assetRoot_);
    soundDirectoryLabel_->setText(directory.isEmpty()
        ? QString::fromUtf8("Папка сигналов не найдена.")
        : QString::fromUtf8("Папка: %1").arg(QDir::toNativeSeparators(directory)));
    soundAvailabilityLabel_->setVisible(!containsUsableSounds(directory));
}
void QtPomodoro::setQuickStateChanged(std::function<void()> handler) {
    quickStateChanged_ = std::move(handler);
    if (quickStateChanged_) quickStateChanged_();
}
QString QtPomodoro::quickSummary() const {
    return phaseName(phase_) + QStringLiteral(" · ") + timeLabel_->text();
}
QString QtPomodoro::quickToggleText() const {
    if (running_) return QString::fromUtf8("Пауза");
    if (awaiting_) return QString::fromUtf8("Начать следующий интервал");
    return remaining_ < duration(phase_) ? QString::fromUtf8("Продолжить") : QString::fromUtf8("Старт фокуса");
}
bool QtPomodoro::quickNextEnabled() const { return !autoAdvance_ || awaiting_; }
void QtPomodoro::quickToggle() {
    if (running_) { running_ = false; timer_->stop(); refresh(); return; }
    if (awaiting_) { phase_ = nextPhase_; awaiting_ = false; remaining_ = duration(phase_); }
    startOrResume();
}
void QtPomodoro::quickNext() {
    if (awaiting_) {
        phase_ = nextPhase_;
        awaiting_ = false;
        remaining_ = duration(phase_);
        startOrResume();
        return;
    }
    if (running_) { finishInterval(false); return; }
    nextPhase_ = phase_ == Work ? (cycles_ >= cyclesBeforeLong_ ? LongBreak : Break) : Work;
    phase_ = nextPhase_;
    remaining_ = duration(phase_);
    startOrResume();
}
void QtPomodoro::quickReset() { reset(); }
void QtPomodoro::playSound(Phase completed) {
    if (!soundEnabled_->isChecked() || soundVolume_->value() <= 0) return;
    const auto relative = completed == Work ? focusSound_->currentData().toString() : breakSound_->currentData().toString();
    if (relative.isEmpty()) { QApplication::beep(); return; }
    const auto path = soundPath(storage_, assetRoot_, relative);
    if (path.isEmpty()) { statusLabel_->setProperty("rewardMessage", QString::fromUtf8("Сигнал не найден; интервал завершён.")); return; }
#ifdef _WIN32
    mciSendStringW(L"close forgemirror_qt_pomodoro", nullptr, 0, nullptr);
    const auto type = path.endsWith(".wav", Qt::CaseInsensitive) ? "waveaudio" : "mpegvideo";
    const auto open = QString("open \"%1\" type %2 alias forgemirror_qt_pomodoro").arg(path, type);
    if (mciSendStringW(reinterpret_cast<LPCWSTR>(open.utf16()), nullptr, 0, nullptr) != 0) {
        statusLabel_->setProperty("rewardMessage", QString::fromUtf8("Не удалось воспроизвести сигнал; интервал завершён.")); return;
    }
    const auto volume = QString("setaudio forgemirror_qt_pomodoro volume to %1").arg(soundVolume_->value() * 10);
    mciSendStringW(reinterpret_cast<LPCWSTR>(volume.utf16()), nullptr, 0, nullptr);
    mciSendStringW(L"play forgemirror_qt_pomodoro from 0", nullptr, 0, nullptr);
#else
    QApplication::beep();
#endif
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
    const auto reward = statusLabel_->property("rewardMessage").toString();
    statusLabel_->setText(!reward.isEmpty() ? reward : awaiting_ ? QString::fromUtf8("Завершено. Следующий интервал: ") + phaseName(nextPhase_) :
        running_ ? QString::fromUtf8("Таймер идёт") : remaining_ < total ? QString::fromUtf8("Пауза") : QString::fromUtf8("Готов к старту"));
    start_->setVisible(!running_ && !awaiting_); start_->setText(remaining_ < total ? QString::fromUtf8("Продолжить") : QString::fromUtf8("Старт фокуса"));
    pause_->setVisible(running_); next_->setVisible(awaiting_); reset_->setEnabled(running_ || awaiting_ || remaining_ < total);
    if (quickStateChanged_) quickStateChanged_();
}
