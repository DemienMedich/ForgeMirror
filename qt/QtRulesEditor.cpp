#include "QtRulesEditor.h"
#include "GameplayConfig.h"
#include <QtWidgets>
#include <QSaveFile>
#include <QTemporaryDir>
#include <cmath>

namespace {
QSpinBox* integerField(QWidget* parent, const char* name, int value, int minimum = 0) {
    auto* field = new QSpinBox(parent); field->setObjectName(name); field->setRange(minimum, 100000000); field->setValue(value); return field;
}
QDoubleSpinBox* factorField(QWidget* parent, const char* name, double value, double maximum) {
    auto* field = new QDoubleSpinBox(parent); field->setObjectName(name); field->setRange(0.0, maximum);
    field->setDecimals(2); field->setSingleStep(0.05); field->setValue(value); return field;
}
bool sameRules(const GameplayConfig& a, const GameplayConfig& b) {
    auto close = [](float x, float y) { return std::abs(x - y) < 0.0001f; };
    return a.levelBaseXp == b.levelBaseXp && a.levelLinearXp == b.levelLinearXp && a.levelQuadraticXp == b.levelQuadraticXp &&
        a.categoryBaseXp == b.categoryBaseXp && close(a.focusBaseBonus, b.focusBaseBonus) && close(a.focusAdditionalBonus, b.focusAdditionalBonus) &&
        close(a.repeatRewardFactor, b.repeatRewardFactor) && close(a.recoveryRewardFactor, b.recoveryRewardFactor) && a.recoveryWarmupTasks == b.recoveryWarmupTasks;
}
QJsonObject rulesObject(const GameplayConfig& value) {
    QJsonArray categories;
    for (const auto category : value.categoryBaseXp) categories.append(category);
    return {{"levelBaseXp", value.levelBaseXp}, {"levelLinearXp", value.levelLinearXp},
        {"levelQuadraticXp", value.levelQuadraticXp}, {"categoryBaseXp", categories},
        {"focusBaseBonus", value.focusBaseBonus}, {"focusAdditionalBonus", value.focusAdditionalBonus},
        {"repeatRewardFactor", value.repeatRewardFactor}, {"recoveryRewardFactor", value.recoveryRewardFactor},
        {"recoveryWarmupTasks", value.recoveryWarmupTasks}};
}
bool parseRules(const QJsonObject& object, GameplayConfig& value) {
    const auto categories = object.value("categoryBaseXp").toArray();
    if (categories.size() != int(Profile::kCategoryCount) || !object.value("levelBaseXp").isDouble() ||
        !object.value("levelLinearXp").isDouble() || !object.value("levelQuadraticXp").isDouble() ||
        !object.value("focusBaseBonus").isDouble() || !object.value("focusAdditionalBonus").isDouble() ||
        !object.value("repeatRewardFactor").isDouble() || !object.value("recoveryRewardFactor").isDouble() ||
        !object.value("recoveryWarmupTasks").isDouble()) return false;
    value.levelBaseXp = object.value("levelBaseXp").toInt(-1);
    value.levelLinearXp = object.value("levelLinearXp").toInt(-1);
    value.levelQuadraticXp = object.value("levelQuadraticXp").toInt(-1);
    for (int i = 0; i < categories.size(); ++i) {
        if (!categories[i].isDouble()) return false;
        value.categoryBaseXp[size_t(i)] = categories[i].toInt(-1);
    }
    value.focusBaseBonus = float(object.value("focusBaseBonus").toDouble(-1));
    value.focusAdditionalBonus = float(object.value("focusAdditionalBonus").toDouble(-1));
    value.repeatRewardFactor = float(object.value("repeatRewardFactor").toDouble(-1));
    value.recoveryRewardFactor = float(object.value("recoveryRewardFactor").toDouble(-1));
    value.recoveryWarmupTasks = object.value("recoveryWarmupTasks").toInt(-1);
    const auto checked = SanitizeGameplayConfig(value);
    if (!sameRules(checked, value)) return false;
    return true;
}
QString jsonPath(const std::filesystem::path& directory, const char* file) {
    return QString::fromUtf8((directory / "meta" / file).u8string());
}
bool readArray(const QString& path, QJsonArray& array) {
    const QFileInfo info(path);
    if (!info.exists()) { array = {}; return true; }
    if (info.isSymLink() || info.isDir() || info.size() > 1024 * 1024) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) return false;
    array = document.array();
    return true;
}
bool writeArray(const QString& path, const QJsonArray& array) {
    const QFileInfo info(path);
    if (info.isSymLink() || info.isDir()) return false;
    if (info.exists() && info.size() > 1024 * 1024) return false;
    QDir().mkpath(info.absolutePath());
    QSaveFile file(path); file.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(array).toJson(QJsonDocument::Indented);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QString rulesChanges(const GameplayConfig& before, const GameplayConfig& after) {
    QStringList changed;
    if (before.levelBaseXp != after.levelBaseXp) changed << QString::fromUtf8("База уровня");
    if (before.levelLinearXp != after.levelLinearXp) changed << QString::fromUtf8("Линейный рост");
    if (before.levelQuadraticXp != after.levelQuadraticXp) changed << QString::fromUtf8("Квадратичный рост");
    for (size_t i = 0; i < before.categoryBaseXp.size(); ++i)
        if (before.categoryBaseXp[i] != after.categoryBaseXp[i])
            changed << QString::fromUtf8("Категория %1").arg(QString::fromUtf8(Profile::kCategoryLabels[i]));
    if (std::abs(before.focusBaseBonus - after.focusBaseBonus) >= 0.0001f) changed << QString::fromUtf8("Базовый фокус-бонус");
    if (std::abs(before.focusAdditionalBonus - after.focusAdditionalBonus) >= 0.0001f) changed << QString::fromUtf8("Дополнительный фокус-бонус");
    if (std::abs(before.repeatRewardFactor - after.repeatRewardFactor) >= 0.0001f) changed << QString::fromUtf8("Повтор награды");
    if (std::abs(before.recoveryRewardFactor - after.recoveryRewardFactor) >= 0.0001f) changed << QString::fromUtf8("Прогрев награды");
    if (before.recoveryWarmupTasks != after.recoveryWarmupTasks) changed << QString::fromUtf8("Число задач прогрева");
    return changed.join(QStringLiteral(", "));
}
void showRulesHistory(QWidget* parent, const std::filesystem::path& directory) {
    QDialog dialog(parent); dialog.setObjectName("rulesHistoryDialog");
    dialog.setWindowTitle(QString::fromUtf8("История правил XP")); dialog.resize(680, 380);
    auto* layout = new QVBoxLayout(&dialog);
    auto* table = new QTableWidget(&dialog); table->setObjectName("rulesHistoryTable");
    table->setAccessibleName(QString::fromUtf8("История изменений правил XP"));
    table->setAccessibleDescription(QString::fromUtf8("Дата изменения и перечень параметров, которые были изменены."));
    table->setColumnCount(2); table->setHorizontalHeaderLabels({QString::fromUtf8("Время"), QString::fromUtf8("Изменённые параметры")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setSelectionBehavior(QAbstractItemView::SelectRows);
    QJsonArray history;
    if (!readArray(jsonPath(directory, "qt-rules-history.json"), history)) {
        layout->addWidget(new QLabel(QString::fromUtf8("Не удалось безопасно прочитать историю правил."), &dialog));
    } else {
        for (int index = history.size() - 1; index >= 0; --index) {
            const auto entry = history[index].toObject();
            const auto time = QDateTime::fromSecsSinceEpoch(entry.value("timestamp").toVariant().toLongLong());
            if (!time.isValid()) continue;
            const int row = table->rowCount(); table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(time.toLocalTime().toString(Qt::ISODate)));
            table->setItem(row, 1, new QTableWidgetItem(entry.value("changes").toString()));
        }
        if (table->rowCount() == 0) layout->addWidget(new QLabel(QString::fromUtf8("Изменений пока нет."), &dialog));
        layout->addWidget(table);
    }
    auto* close = new QPushButton(QString::fromUtf8("Закрыть")); layout->addWidget(close, 0, Qt::AlignRight);
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}
}

bool ShowRulesEditor(QWidget* parent, QtWorkspace& workspace) {
    QDialog dialog(parent); dialog.setObjectName("rulesEditor"); dialog.setWindowTitle(QString::fromUtf8("Правила прогресса"));
    dialog.resize(560, 620); dialog.setMinimumSize(480, 500);
    auto* outer = new QVBoxLayout(&dialog);
    auto* hint = new QLabel(QString::fromUtf8("Изменения влияют на будущие расчёты XP. Накопленный прогресс профилей автоматически не пересчитывается."));
    hint->setWordWrap(true); outer->addWidget(hint);
    auto* presetRow = new QHBoxLayout;
    auto* presetCombo = new QComboBox; presetCombo->setObjectName("rulesPresetCombo");
    presetCombo->setAccessibleName(QString::fromUtf8("Локальный пресет правил XP"));
    presetCombo->setAccessibleDescription(QString::fromUtf8("Выберите сохранённый пресет для просмотра или загрузки в редактор."));
    auto* savePreset = new QPushButton(QString::fromUtf8("Сохранить пресет")); savePreset->setObjectName("rulesSavePreset");
    auto* applyPreset = new QPushButton(QString::fromUtf8("Загрузить")); applyPreset->setObjectName("rulesApplyPreset");
    auto* deletePreset = new QPushButton(QString::fromUtf8("Удалить")); deletePreset->setObjectName("rulesDeletePreset");
    presetRow->addWidget(new QLabel(QString::fromUtf8("Локальный пресет:"))); presetRow->addWidget(presetCombo, 1);
    presetRow->addWidget(savePreset); presetRow->addWidget(applyPreset); presetRow->addWidget(deletePreset);
    outer->addLayout(presetRow);
    auto* historyRow = new QHBoxLayout; historyRow->addStretch();
    auto* historyButton = new QPushButton(QString::fromUtf8("История изменений")); historyButton->setObjectName("rulesHistory");
    historyRow->addWidget(historyButton); outer->addLayout(historyRow);
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); outer->addWidget(scroll, 1);
    auto* content = new QWidget; auto* form = new QFormLayout(content); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const auto current = workspace.data.rulesConfig;
    const auto presetsPath = jsonPath(workspace.directory, "qt-rules-presets.json");
    auto refreshPresets = [&] {
        presetCombo->clear();
        QJsonArray presets;
        if (!readArray(presetsPath, presets)) return false;
        for (const auto& value : presets) {
            const auto preset = value.toObject(); const auto name = preset.value("name").toString().trimmed();
            GameplayConfig ignored;
            if (!name.isEmpty() && parseRules(preset.value("rules").toObject(), ignored)) presetCombo->addItem(name, name);
        }
        return true;
    };
    auto* levelBase = integerField(content, "rulesLevelBase", current.levelBaseXp, 1);
    auto* levelLinear = integerField(content, "rulesLevelLinear", current.levelLinearXp);
    auto* levelQuadratic = integerField(content, "rulesLevelQuadratic", current.levelQuadraticXp);
    form->addRow(QString::fromUtf8("Базовый XP уровня"), levelBase);
    form->addRow(QString::fromUtf8("Линейный прирост"), levelLinear);
    form->addRow(QString::fromUtf8("Квадратичный прирост"), levelQuadratic);
    std::array<QSpinBox*, Profile::kCategoryCount> categories{};
    std::array<QLabel*, Profile::kCategoryCount> categoryLabels{};
    for (size_t i = 0; i < categories.size(); ++i) {
        categories[i] = integerField(content, ("rulesCategory" + std::to_string(i)).c_str(), current.categoryBaseXp[i]);
        categoryLabels[i] = new QLabel(QString::fromUtf8("Категория %1 · базовый XP").arg(QString::fromUtf8(Profile::kCategoryLabels[i])), content);
        categoryLabels[i]->setObjectName(QString::fromLatin1("rulesCategoryLabel%1").arg(int(i)));
        categoryLabels[i]->setBuddy(categories[i]);
        form->addRow(categoryLabels[i], categories[i]);
    }
    auto* categoryAttention = new QLabel(content);
    categoryAttention->setObjectName("rulesCategoryAttention");
    categoryAttention->setWordWrap(true);
    categoryAttention->setStyleSheet(QStringLiteral("color: palette(link); font-weight: 600;"));
    form->addRow(categoryAttention);
    auto refreshCategoryAttention = [&] {
        const auto minIt = std::min_element(categories.begin(), categories.end(), [](const auto* lhs, const auto* rhs) {
            return lhs->value() < rhs->value();
        });
        const int minimum = (*minIt)->value();
        const int maximum = (*std::max_element(categories.begin(), categories.end(), [](const auto* lhs, const auto* rhs) {
            return lhs->value() < rhs->value();
        }))->value();
        const bool show = categories.size() >= 2 && maximum - minimum >= 2;
        const int minIndex = int(std::distance(categories.begin(), minIt));
        categoryAttention->setVisible(show);
        categoryAttention->setText(show
            ? QString::fromUtf8("Зона внимания: категория %1 (%2 XP)")
                .arg(QString::fromUtf8(Profile::kCategoryLabels[size_t(minIndex)])).arg(minimum)
            : QString{});
        for (size_t i = 0; i < categoryLabels.size(); ++i) {
            auto font = categoryLabels[i]->font();
            font.setBold(show && int(i) == minIndex);
            categoryLabels[i]->setFont(font);
        }
    };
    for (auto* category : categories)
        QObject::connect(category, qOverload<int>(&QSpinBox::valueChanged), &dialog, refreshCategoryAttention);
    refreshCategoryAttention();
    auto* focusBase = factorField(content, "rulesFocusBase", current.focusBaseBonus, 10.0);
    auto* focusExtra = factorField(content, "rulesFocusExtra", current.focusAdditionalBonus, 10.0);
    auto* repeat = factorField(content, "rulesRepeat", current.repeatRewardFactor, 1.0);
    auto* recovery = factorField(content, "rulesRecovery", current.recoveryRewardFactor, 1.0);
    auto* warmup = integerField(content, "rulesWarmup", current.recoveryWarmupTasks);
    form->addRow(QString::fromUtf8("Базовый фокус-бонус"), focusBase);
    form->addRow(QString::fromUtf8("Дополнительный фокус-бонус"), focusExtra);
    form->addRow(QString::fromUtf8("Коэффициент повтора"), repeat);
    form->addRow(QString::fromUtf8("Коэффициент прогрева"), recovery);
    form->addRow(QString::fromUtf8("Задач прогрева"), warmup);
    auto* notice = new QLabel; notice->setObjectName("rulesNotice"); notice->setWordWrap(true); notice->setTextFormat(Qt::PlainText); form->addRow(notice);
    scroll->setWidget(content);
    auto collectDraft = [&] {
        GameplayConfig draft;
        draft.levelBaseXp = levelBase->value(); draft.levelLinearXp = levelLinear->value(); draft.levelQuadraticXp = levelQuadratic->value();
        for (size_t i = 0; i < categories.size(); ++i) draft.categoryBaseXp[i] = categories[i]->value();
        draft.focusBaseBonus = float(focusBase->value()); draft.focusAdditionalBonus = float(focusExtra->value());
        draft.repeatRewardFactor = float(repeat->value()); draft.recoveryRewardFactor = float(recovery->value());
        draft.recoveryWarmupTasks = warmup->value();
        return SanitizeGameplayConfig(draft);
    };
    auto applyDraft = [&](const GameplayConfig& draft) {
        levelBase->setValue(draft.levelBaseXp); levelLinear->setValue(draft.levelLinearXp); levelQuadratic->setValue(draft.levelQuadraticXp);
        for (size_t i = 0; i < categories.size(); ++i) categories[i]->setValue(draft.categoryBaseXp[i]);
        focusBase->setValue(draft.focusBaseBonus); focusExtra->setValue(draft.focusAdditionalBonus);
        repeat->setValue(draft.repeatRewardFactor); recovery->setValue(draft.recoveryRewardFactor); warmup->setValue(draft.recoveryWarmupTasks);
    };
    if (!refreshPresets()) {
        notice->setText(QString::fromUtf8("Файл локальных пресетов повреждён или недоступен."));
        presetCombo->setEnabled(false); savePreset->setEnabled(false); applyPreset->setEnabled(false); deletePreset->setEnabled(false);
    }
    QObject::connect(savePreset, &QPushButton::clicked, &dialog, [&] {
        bool accepted = false;
        const auto name = QInputDialog::getText(&dialog, QString::fromUtf8("Сохранить пресет"),
            QString::fromUtf8("Название (до 48 символов):"), QLineEdit::Normal, {}, &accepted).trimmed();
        if (!accepted) return;
        if (name.isEmpty() || name.size() > 48 || name.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")))) {
            notice->setText(QString::fromUtf8("Название должно содержать от 1 до 48 печатных символов.")); return;
        }
        QJsonArray presets; if (!readArray(presetsPath, presets)) { notice->setText(QString::fromUtf8("Не удалось прочитать пресеты.")); return; }
        const auto draft = collectDraft(); int existing = -1;
        for (int i = 0; i < presets.size(); ++i)
            if (presets[i].toObject().value("name").toString().compare(name, Qt::CaseInsensitive) == 0) { existing = i; break; }
        if (existing < 0 && presets.size() >= 20) { notice->setText(QString::fromUtf8("Можно хранить не более 20 пресетов.")); return; }
        const QJsonObject value{{"name", name}, {"rules", rulesObject(draft)}};
        if (existing >= 0) presets[existing] = value; else presets.append(value);
        if (!writeArray(presetsPath, presets)) { notice->setText(QString::fromUtf8("Не удалось безопасно сохранить пресет.")); return; }
        refreshPresets(); presetCombo->setCurrentIndex(presetCombo->findData(name));
        notice->setText(QString::fromUtf8("Пресет сохранён локально. Текущие правила ещё не применены."));
    });
    QObject::connect(applyPreset, &QPushButton::clicked, &dialog, [&] {
        QJsonArray presets; if (!readArray(presetsPath, presets)) { notice->setText(QString::fromUtf8("Не удалось прочитать пресеты.")); return; }
        for (const auto& value : presets) {
            const auto preset = value.toObject();
            if (preset.value("name").toString() != presetCombo->currentData().toString()) continue;
            GameplayConfig draft;
            if (!parseRules(preset.value("rules").toObject(), draft)) { notice->setText(QString::fromUtf8("Пресет содержит некорректные правила.")); return; }
            applyDraft(draft); notice->setText(QString::fromUtf8("Пресет загружен в форму. Сохраните правила для применения.")); return;
        }
        notice->setText(QString::fromUtf8("Выберите существующий пресет."));
    });
    QObject::connect(deletePreset, &QPushButton::clicked, &dialog, [&] {
        const auto name = presetCombo->currentData().toString(); if (name.isEmpty()) return;
        if (QMessageBox::question(&dialog, QString::fromUtf8("Удалить пресет"),
            QString::fromUtf8("Удалить локальный пресет «%1»?").arg(name)) != QMessageBox::Yes) return;
        QJsonArray presets; if (!readArray(presetsPath, presets)) { notice->setText(QString::fromUtf8("Не удалось прочитать пресеты.")); return; }
        for (int i = presets.size() - 1; i >= 0; --i)
            if (presets[i].toObject().value("name").toString() == name) presets.removeAt(i);
        if (!writeArray(presetsPath, presets)) { notice->setText(QString::fromUtf8("Не удалось удалить пресет.")); return; }
        refreshPresets(); notice->setText(QString::fromUtf8("Пресет удалён."));
    });
    QObject::connect(historyButton, &QPushButton::clicked, &dialog, [&] { showRulesHistory(&dialog, workspace.directory); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить")); buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена")); outer->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) { notice->setText(QString::fromUtf8("Сначала завершите восстановление данных.")); return; }
        const auto draft = collectDraft();
        QTemporaryDir staging; if (!staging.isValid()) { notice->setText(QString::fromUtf8("Не удалось создать временный каталог.")); return; }
        const auto stagingDir = std::filesystem::u8path(staging.path().toUtf8().toStdString());
        if (!SaveGameplayConfig(draft, stagingDir)) { notice->setText(QString::fromUtf8("Не удалось сериализовать правила.")); return; }
        const auto checked = LoadGameplayConfig(stagingDir);
        QFile input(staging.path() + "/meta/gameplay.ini"); if (!input.open(QIODevice::ReadOnly)) { notice->setText(QString::fromUtf8("Не удалось проверить файл правил.")); return; }
        const auto bytes = input.readAll();
        if (!sameRules(checked, draft) || !bytes.startsWith("\xEF\xBB\xBF")) { notice->setText(QString::fromUtf8("Проверка правил не пройдена.")); return; }
        const auto target = QString::fromUtf8((workspace.directory / "meta/gameplay.ini").u8string());
        if (QFileInfo(QFileInfo(target).absolutePath()).isSymLink() || QFileInfo(target).isSymLink()) { notice->setText(QString::fromUtf8("Символьная ссылка gameplay.ini не поддерживается.")); return; }
        QDir().mkpath(QFileInfo(target).absolutePath()); QSaveFile output(target); output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) { notice->setText(QString::fromUtf8("Не удалось атомарно сохранить правила.")); return; }
        if (!sameRules(current, draft)) {
            QJsonArray history; const auto historyPath = jsonPath(workspace.directory, "qt-rules-history.json");
            if (!readArray(historyPath, history)) {
                QMessageBox::warning(&dialog, QString::fromUtf8("История правил"), QString::fromUtf8("Правила сохранены, но история изменений повреждена или недоступна."));
            } else {
                history.append(QJsonObject{{"timestamp", QDateTime::currentSecsSinceEpoch()}, {"before", rulesObject(current)},
                    {"after", rulesObject(draft)}, {"changes", rulesChanges(current, draft)}});
                while (history.size() > 100) history.removeFirst();
                if (!writeArray(historyPath, history))
                    QMessageBox::warning(&dialog, QString::fromUtf8("История правил"), QString::fromUtf8("Правила сохранены, но не удалось записать историю изменений."));
            }
        }
        workspace.data.rulesConfig = draft; SetGameplayConfig(draft); dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
