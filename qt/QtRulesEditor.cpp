#include "QtRulesEditor.h"
#include "GameplayConfig.h"
#include "QtScrollableDialog.h"
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
// Keep QInputDialog compatibility while invalid input remains a local draft.
class RulesPresetNameDialog final : public QInputDialog {
public:
    explicit RulesPresetNameDialog(QtScrollableDialog* parent) : QInputDialog(parent) {
        setObjectName("rulesPresetNameDialog");
        setWindowTitle(QString::fromUtf8("Сохранить пресет"));
        setInputMode(QInputDialog::TextInput);
        setLabelText(QString::fromUtf8("Название пресета"));
        setOkButtonText(QString::fromUtf8("Сохранить")); setCancelButtonText(QString::fromUtf8("Отмена"));
        input_ = findChild<QLineEdit*>();
        if (input_) {
            input_->setObjectName("rulesPresetName");
            input_->setAccessibleName(QString::fromUtf8("Название локального пресета правил XP"));
            input_->setAccessibleDescription(nameDetails()); input_->setToolTip(nameDetails());
        }
        auto* column = qobject_cast<QVBoxLayout*>(layout());
        auto* buttons = findChild<QDialogButtonBox*>();
        auto* hint = new QLabel(nameDetails(), this);
        hint->setObjectName("rulesPresetNameHint"); hint->setTextFormat(Qt::PlainText); hint->setWordWrap(true);
        notice_ = new QLabel(this); notice_->setObjectName("rulesPresetNameNotice");
        notice_->setTextFormat(Qt::PlainText); notice_->setWordWrap(true);
        notice_->setAccessibleName(QString::fromUtf8("Ошибка имени пресета")); notice_->hide();
        auto* footer = new QWidget(this); footer->setObjectName("dialogFooter");
        auto* footerLayout = new QVBoxLayout(footer); footerLayout->setContentsMargins(0, 0, 0, 0);
        footerLayout->setSpacing(parent->scaledMetric(8)); footerLayout->addWidget(notice_);
        if (column && buttons) {
            column->removeWidget(buttons); footerLayout->addWidget(buttons);
            column->addWidget(hint); column->addWidget(footer);
            column->setSpacing(parent->scaledMetric(8)); column->setSizeConstraint(QLayout::SetMinimumSize);
        } else if (layout()) {
            layout()->addWidget(hint); layout()->addWidget(footer);
            if (buttons) footerLayout->addWidget(buttons);
            layout()->setSizeConstraint(QLayout::SetMinimumSize);
        }
        for (auto* label : findChildren<QLabel*>()) { label->setTextFormat(Qt::PlainText); label->setWordWrap(true); }
        if (buttons) {
            buttons->setObjectName("rulesPresetNameButtons");
            auto* save = buttons->button(QDialogButtonBox::Ok);
            auto* cancel = buttons->button(QDialogButtonBox::Cancel);
            if (save) {
                save->setObjectName("rulesPresetNameSave"); save->setProperty("primary", true);
                save->setAccessibleName(QString::fromUtf8("Сохранить локальный пресет"));
                save->setMinimumHeight(parent->scaledMetric(32)); save->setAutoDefault(true); save->setDefault(true);
                save->style()->unpolish(save); save->style()->polish(save); save->update();
            }
            if (cancel) { cancel->setObjectName("rulesPresetNameCancel"); cancel->setAutoDefault(false);
                cancel->setAccessibleName(QString::fromUtf8("Отменить сохранение пресета")); }
            if (input_ && save) QWidget::setTabOrder(input_, save);
            if (save && cancel) QWidget::setTabOrder(save, cancel);
        }
        QObject::connect(this, &QInputDialog::textValueChanged, this, [this](const QString& text) {
            if (validName(text.trimmed())) { notice_->clear(); notice_->hide();
                if (input_) input_->setAccessibleDescription(nameDetails()); }
        });
        setMinimumSize(420, 220); setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX); resize(560, 320);
    }
    void accept() override {
        if (!validName(textValue().trimmed())) {
            notice_->setText(QString::fromUtf8("Название должно содержать от 1 до 48 печатных символов."));
            notice_->setAccessibleDescription(notice_->text()); notice_->show();
            if (layout()) {
                layout()->invalidate(); layout()->activate();
                const int needed = layout()->hasHeightForWidth() ? layout()->heightForWidth(width()) : sizeHint().height();
                resize(width(), std::max(height(), std::max(minimumSizeHint().height(), needed)));
            }
            boundToScreen();
            if (input_) { input_->setAccessibleDescription(notice_->text()); input_->setFocus(Qt::OtherFocusReason); }
            return;
        }
        QInputDialog::accept();
    }
protected:
    void showEvent(QShowEvent* event) override { QInputDialog::showEvent(event); boundToScreen(); }
    void keyPressEvent(QKeyEvent* event) override {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
            (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::KeypadModifier)) {
            auto* button = qobject_cast<QPushButton*>(focusWidget());
            if (button && button->isVisible() && button->isEnabled()) { event->accept(); button->click(); return; }
        }
        QInputDialog::keyPressEvent(event);
    }
private:
    static QString nameDetails() { return QString::fromUtf8("От 1 до 48 печатных символов. Пресет сохраняет значения формы, не применяя правила."); }
    static bool validName(const QString& name) {
        return !name.isEmpty() && name.size() <= 48 && !name.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")));
    }
    void boundToScreen() {
        auto* currentScreen = windowHandle() ? windowHandle()->screen() : screen();
        if (!currentScreen) return;
        const auto available = currentScreen->availableGeometry();
        const QSize extra(std::max(0, frameGeometry().width() - width()), std::max(0, frameGeometry().height() - height()));
        const QSize limit(std::max(1, available.width() - extra.width() - 16), std::max(1, available.height() - extra.height() - 16));
        setMinimumSize(QSize(420, 220).boundedTo(limit)); setMaximumSize(limit); resize(size().boundedTo(limit));
        const auto frame = frameGeometry();
        const int x = std::clamp(frame.left(), available.left(), std::max(available.left(), available.right() - frame.width() + 1));
        const int y = std::clamp(frame.top(), available.top(), std::max(available.top(), available.bottom() - frame.height() + 1));
        move(pos() + QPoint(x - frame.left(), y - frame.top()));
    }
    QLineEdit* input_ = nullptr;
    QLabel* notice_ = nullptr;
};
void showRulesHistory(QWidget* parent, const std::filesystem::path& directory) {
    QtScrollableDialog dialog(parent, QSize(640, 520)); dialog.setObjectName("rulesHistoryDialog");
    dialog.setWindowTitle(QString::fromUtf8("История правил XP"));
    dialog.scrollArea()->setAccessibleName(QString::fromUtf8("История изменений правил XP"));
    auto* form = dialog.formLayout();
    form->setHorizontalSpacing(dialog.scaledMetric(8)); form->setVerticalSpacing(dialog.scaledMetric(8));
    form->setFormAlignment(Qt::AlignTop);
    const auto addMessage = [&](const QString& message) {
        auto* label = new QLabel(message, dialog.bodyWidget());
        label->setObjectName("rulesHistoryNotice");
        label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
        label->setAccessibleName(QString::fromUtf8("Состояние истории правил"));
        label->setAccessibleDescription(message);
        form->addRow(label);
    };
    auto* table = new QTableWidget(&dialog); table->setObjectName("rulesHistoryTable");
    table->setAccessibleName(QString::fromUtf8("История изменений правил XP"));
    table->setAccessibleDescription(QString::fromUtf8("Дата изменения и перечень параметров, которые были изменены."));
    table->setColumnCount(2); table->setHorizontalHeaderLabels({QString::fromUtf8("Время"), QString::fromUtf8("Изменения")});
    table->horizontalHeaderItem(1)->setToolTip(QString::fromUtf8("Изменённые параметры правил XP"));
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setWordWrap(true); table->setTextElideMode(Qt::ElideNone);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setMinimumHeight(dialog.scaledMetric(128));
    table->verticalHeader()->hide();
    QObject::connect(table->horizontalHeader(), &QHeaderView::sectionResized, table, [table] { table->resizeRowsToContents(); });
    QJsonArray history;
    if (!readArray(jsonPath(directory, "qt-rules-history.json"), history)) {
        table->hide();
        addMessage(QString::fromUtf8("Не удалось безопасно прочитать историю правил."));
    } else {
        for (int index = history.size() - 1; index >= 0; --index) {
            const auto entry = history[index].toObject();
            const auto time = QDateTime::fromSecsSinceEpoch(entry.value("timestamp").toVariant().toLongLong());
            if (!time.isValid()) continue;
            const int row = table->rowCount(); table->insertRow(row);
            const auto timestamp = time.toLocalTime().toString(Qt::ISODate);
            auto* date = new QTableWidgetItem(time.toLocalTime().toString(QStringLiteral("dd.MM.yyyy\nHH:mm:ss")));
            date->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
            date->setToolTip(timestamp); date->setData(Qt::AccessibleTextRole, timestamp);
            auto* changes = new QTableWidgetItem(entry.value("changes").toString());
            changes->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
            changes->setToolTip(changes->text()); changes->setData(Qt::AccessibleTextRole, changes->text());
            table->setItem(row, 0, date); table->setItem(row, 1, changes);
        }
        if (table->rowCount() == 0) { addMessage(QString::fromUtf8("Изменений пока нет.")); table->hide(); }
        else form->addRow(table);
        table->resizeRowsToContents();
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->setObjectName("rulesHistoryButtons");
    auto* close = buttons->button(QDialogButtonBox::Close);
    close->setObjectName("rulesHistoryClose"); close->setText(QString::fromUtf8("Закрыть"));
    close->setAccessibleName(QString::fromUtf8("Закрыть историю правил XP"));
    close->setProperty("primary", true); close->setMinimumHeight(dialog.scaledMetric(32));
    close->setAutoDefault(true); close->setDefault(true);
    dialog.footerLayout()->addWidget(buttons);
    if (table->rowCount() > 0) { QWidget::setTabOrder(table, close); table->setFocus(); }
    else close->setFocus();
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}
}

bool ShowRulesEditor(QWidget* parent, QtWorkspace& workspace) {
    QtScrollableDialog dialog(parent, QSize(640, 520));
    dialog.setObjectName("rulesEditor"); dialog.setWindowTitle(QString::fromUtf8("Правила прогресса"));
    dialog.scrollArea()->setAccessibleName(QString::fromUtf8("Поля правил прогресса и локальные пресеты"));
    auto* content = dialog.bodyWidget(); auto* form = dialog.formLayout();
    form->setHorizontalSpacing(dialog.scaledMetric(8)); form->setVerticalSpacing(dialog.scaledMetric(8));
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop); form->setFormAlignment(Qt::AlignTop);
    const auto outerMargins = dialog.layout()->contentsMargins();
    const int preferredFormWidth = dialog.width() - outerMargins.left() - outerMargins.right()
        - dialog.style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    const auto addField = [form, preferredFormWidth](const QString& caption, const QString& details, QWidget* field) {
        auto* label = new QLabel(caption);
        label->setObjectName(field->objectName() + QStringLiteral("Label"));
        label->setTextFormat(Qt::PlainText); label->setWordWrap(true); label->setBuddy(field);
        label->setToolTip(details);
        field->setAccessibleName(details); field->setAccessibleDescription(details); field->setToolTip(details);
        form->addRow(label, field);
        if (auto* spin = qobject_cast<QAbstractSpinBox*>(field)) {
            spin->ensurePolished();
            spin->setMinimumWidth(spin->sizeHint().width());
            // Preserve the complete numeric editor instead of squeezing it
            // beside a caption wider than the preferred form can accommodate.
            if (label->sizeHint().width() + spin->minimumWidth() + form->horizontalSpacing() > preferredFormWidth)
                form->setRowWrapPolicy(QFormLayout::WrapAllRows);
        }
    };
    const auto addHeading = [form](const QString& caption) {
        auto* label = new QLabel(caption); label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
        auto font = label->font(); font.setWeight(QFont::DemiBold); label->setFont(font);
        form->addRow(label);
    };
    auto* hint = new QLabel(QString::fromUtf8("Изменения влияют на будущие расчёты XP. Накопленный прогресс профилей автоматически не пересчитывается."));
    hint->setObjectName("rulesEditorHint"); hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true); form->addRow(hint);
    auto* presetCombo = new QComboBox; presetCombo->setObjectName("rulesPresetCombo");
    presetCombo->setAccessibleName(QString::fromUtf8("Локальный пресет правил XP"));
    presetCombo->setAccessibleDescription(QString::fromUtf8("Выберите сохранённый пресет для просмотра или загрузки в редактор."));
    presetCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    presetCombo->setMinimumContentsLength(8);
    presetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* savePreset = new QPushButton(QString::fromUtf8("Сохранить пресет")); savePreset->setObjectName("rulesSavePreset");
    auto* applyPreset = new QPushButton(QString::fromUtf8("Загрузить")); applyPreset->setObjectName("rulesApplyPreset");
    auto* deletePreset = new QPushButton(QString::fromUtf8("Удалить")); deletePreset->setObjectName("rulesDeletePreset");
    auto* historyButton = new QPushButton(QString::fromUtf8("История изменений")); historyButton->setObjectName("rulesHistory");
    auto* presetLabel = new QLabel(QString::fromUtf8("Локальный пресет"));
    presetLabel->setObjectName("rulesPresetLabel"); presetLabel->setTextFormat(Qt::PlainText);
    presetLabel->setWordWrap(true); presetLabel->setBuddy(presetCombo);
    form->addRow(presetLabel, presetCombo);
    auto* presetCommands = new QtDialogFlowRow(content, dialog.scaledMetric(8), dialog.scaledMetric(4));
    presetCommands->setObjectName("rulesPresetCommands");
    const QStringList commandDetails = {QString::fromUtf8("Сохранить значения формы в локальный пресет, не применяя текущие правила."),
        QString::fromUtf8("Загрузить выбранный пресет в черновик формы."),
        QString::fromUtf8("Удалить выбранный локальный пресет после подтверждения."), QString::fromUtf8("Открыть историю изменений правил XP.")};
    const QList<QPushButton*> commands{savePreset, applyPreset, deletePreset, historyButton};
    for (int index = 0; index < commands.size(); ++index) {
        auto* command = commands[index]; command->setAutoDefault(false); command->setDefault(false);
        command->setAccessibleName(command->text()); command->setAccessibleDescription(commandDetails[index]);
        command->setToolTip(commandDetails[index]); presetCommands->addWidget(command);
    }
    form->addRow(presetCommands);
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
    addHeading(QString::fromUtf8("Уровни"));
    addField(QString::fromUtf8("Базовый XP"), QString::fromUtf8("Базовый XP уровня"), levelBase);
    addField(QString::fromUtf8("Линейный прирост"), QString::fromUtf8("Линейный прирост XP уровня"), levelLinear);
    addField(QString::fromUtf8("Квадратичный прирост"), QString::fromUtf8("Квадратичный прирост XP уровня"), levelQuadratic);
    std::array<QSpinBox*, Profile::kCategoryCount> categories{};
    std::array<QLabel*, Profile::kCategoryCount> categoryLabels{};
    addHeading(QString::fromUtf8("Категории · базовый XP"));
    for (size_t i = 0; i < categories.size(); ++i) {
        categories[i] = integerField(content, ("rulesCategory" + std::to_string(i)).c_str(), current.categoryBaseXp[i]);
        const auto details = QString::fromUtf8("Категория %1 · базовый XP").arg(QString::fromUtf8(Profile::kCategoryLabels[i]));
        categoryLabels[i] = new QLabel(QString::fromUtf8("Категория %1").arg(QString::fromUtf8(Profile::kCategoryLabels[i])), content);
        categoryLabels[i]->setObjectName(QString::fromLatin1("rulesCategoryLabel%1").arg(int(i)));
        categoryLabels[i]->setBuddy(categories[i]);
        categoryLabels[i]->setTextFormat(Qt::PlainText); categoryLabels[i]->setWordWrap(true);
        categoryLabels[i]->setToolTip(details);
        categories[i]->setAccessibleName(details); categories[i]->setAccessibleDescription(details); categories[i]->setToolTip(details);
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
    addHeading(QString::fromUtf8("Фокус и награды"));
    addField(QString::fromUtf8("Базовый бонус"), QString::fromUtf8("Базовый фокус-бонус"), focusBase);
    addField(QString::fromUtf8("Дополнительный бонус"), QString::fromUtf8("Дополнительный фокус-бонус"), focusExtra);
    addField(QString::fromUtf8("Повтор награды"), QString::fromUtf8("Коэффициент повтора награды"), repeat);
    addField(QString::fromUtf8("Прогрев награды"), QString::fromUtf8("Коэффициент прогрева награды"), recovery);
    addField(QString::fromUtf8("Задач прогрева"), QString::fromUtf8("Число задач прогрева награды"), warmup);
    auto* notice = new QLabel; notice->setObjectName("rulesNotice"); notice->setWordWrap(true); notice->setTextFormat(Qt::PlainText);
    notice->setTextInteractionFlags(Qt::TextSelectableByMouse);
    notice->setAccessibleName(QString::fromUtf8("Результат изменения правил или пресетов"));
    dialog.footerLayout()->addWidget(notice);
    notice->hide();
    const auto showNotice = [notice](const QString& message) {
        notice->setText(message); notice->setAccessibleDescription(message); notice->show();
    };
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
        showNotice(QString::fromUtf8("Файл локальных пресетов повреждён или недоступен."));
        presetCombo->setEnabled(false); savePreset->setEnabled(false); applyPreset->setEnabled(false); deletePreset->setEnabled(false);
    }
    QObject::connect(savePreset, &QPushButton::clicked, &dialog, [&] {
        RulesPresetNameDialog input(&dialog);
        if (input.exec() != QDialog::Accepted) return;
        const auto name = input.textValue().trimmed();
        if (name.isEmpty() || name.size() > 48 || name.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")))) {
            showNotice(QString::fromUtf8("Название должно содержать от 1 до 48 печатных символов.")); return;
        }
        QJsonArray presets; if (!readArray(presetsPath, presets)) { showNotice(QString::fromUtf8("Не удалось прочитать пресеты.")); return; }
        const auto draft = collectDraft(); int existing = -1;
        for (int i = 0; i < presets.size(); ++i)
            if (presets[i].toObject().value("name").toString().compare(name, Qt::CaseInsensitive) == 0) { existing = i; break; }
        if (existing < 0 && presets.size() >= 20) { showNotice(QString::fromUtf8("Можно хранить не более 20 пресетов.")); return; }
        const QJsonObject value{{"name", name}, {"rules", rulesObject(draft)}};
        if (existing >= 0) presets[existing] = value; else presets.append(value);
        if (!writeArray(presetsPath, presets)) { showNotice(QString::fromUtf8("Не удалось безопасно сохранить пресет.")); return; }
        refreshPresets(); presetCombo->setCurrentIndex(presetCombo->findData(name));
        showNotice(QString::fromUtf8("Пресет сохранён локально. Текущие правила ещё не применены."));
    });
    QObject::connect(applyPreset, &QPushButton::clicked, &dialog, [&] {
        QJsonArray presets; if (!readArray(presetsPath, presets)) { showNotice(QString::fromUtf8("Не удалось прочитать пресеты.")); return; }
        for (const auto& value : presets) {
            const auto preset = value.toObject();
            if (preset.value("name").toString() != presetCombo->currentData().toString()) continue;
            GameplayConfig draft;
            if (!parseRules(preset.value("rules").toObject(), draft)) { showNotice(QString::fromUtf8("Пресет содержит некорректные правила.")); return; }
            applyDraft(draft); showNotice(QString::fromUtf8("Пресет загружен в форму. Сохраните правила для применения.")); return;
        }
        showNotice(QString::fromUtf8("Выберите существующий пресет."));
    });
    QObject::connect(deletePreset, &QPushButton::clicked, &dialog, [&] {
        const auto name = presetCombo->currentData().toString(); if (name.isEmpty()) return;
        if (QMessageBox::question(&dialog, QString::fromUtf8("Удалить пресет"),
            QString::fromUtf8("Удалить локальный пресет «%1»?").arg(name)) != QMessageBox::Yes) return;
        QJsonArray presets; if (!readArray(presetsPath, presets)) { showNotice(QString::fromUtf8("Не удалось прочитать пресеты.")); return; }
        for (int i = presets.size() - 1; i >= 0; --i)
            if (presets[i].toObject().value("name").toString() == name) presets.removeAt(i);
        if (!writeArray(presetsPath, presets)) { showNotice(QString::fromUtf8("Не удалось удалить пресет.")); return; }
        refreshPresets(); showNotice(QString::fromUtf8("Пресет удалён."));
    });
    QObject::connect(historyButton, &QPushButton::clicked, &dialog, [&] { showRulesHistory(&dialog, workspace.directory); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName("rulesEditorButtons");
    auto* save = buttons->button(QDialogButtonBox::Save); auto* cancel = buttons->button(QDialogButtonBox::Cancel);
    save->setObjectName("rulesSave"); cancel->setObjectName("rulesCancel");
    save->setText(QString::fromUtf8("Сохранить")); save->setProperty("primary", true);
    save->setAccessibleName(QString::fromUtf8("Применить и сохранить правила прогресса"));
    save->setMinimumHeight(dialog.scaledMetric(32)); save->setAutoDefault(true); save->setDefault(true);
    cancel->setText(QString::fromUtf8("Отмена")); cancel->setAccessibleName(QString::fromUtf8("Отменить изменение правил"));
    cancel->setAutoDefault(false); dialog.footerLayout()->addWidget(buttons);
    QList<QWidget*> tabFields{presetCombo, savePreset, applyPreset, deletePreset, historyButton, levelBase, levelLinear, levelQuadratic};
    for (auto* category : categories) tabFields.append(category);
    tabFields << focusBase << focusExtra << repeat << recovery << warmup << save << cancel;
    for (int index = 1; index < tabFields.size(); ++index) QWidget::setTabOrder(tabFields[index - 1], tabFields[index]);
    presetCombo->setFocus();
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) { showNotice(QString::fromUtf8("Сначала завершите восстановление данных.")); return; }
        const auto draft = collectDraft();
        QTemporaryDir staging; if (!staging.isValid()) { showNotice(QString::fromUtf8("Не удалось создать временный каталог.")); return; }
        const auto stagingDir = std::filesystem::u8path(staging.path().toUtf8().toStdString());
        if (!SaveGameplayConfig(draft, stagingDir)) { showNotice(QString::fromUtf8("Не удалось сериализовать правила.")); return; }
        const auto checked = LoadGameplayConfig(stagingDir);
        QFile input(staging.path() + "/meta/gameplay.ini"); if (!input.open(QIODevice::ReadOnly)) { showNotice(QString::fromUtf8("Не удалось проверить файл правил.")); return; }
        const auto bytes = input.readAll();
        if (!sameRules(checked, draft) || !bytes.startsWith("\xEF\xBB\xBF")) { showNotice(QString::fromUtf8("Проверка правил не пройдена.")); return; }
        const auto target = QString::fromUtf8((workspace.directory / "meta/gameplay.ini").u8string());
        if (QFileInfo(QFileInfo(target).absolutePath()).isSymLink() || QFileInfo(target).isSymLink()) { showNotice(QString::fromUtf8("Символьная ссылка gameplay.ini не поддерживается.")); return; }
        QDir().mkpath(QFileInfo(target).absolutePath()); QSaveFile output(target); output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) { showNotice(QString::fromUtf8("Не удалось атомарно сохранить правила.")); return; }
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
