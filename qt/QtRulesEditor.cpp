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
}

bool ShowRulesEditor(QWidget* parent, QtWorkspace& workspace) {
    QDialog dialog(parent); dialog.setObjectName("rulesEditor"); dialog.setWindowTitle(QString::fromUtf8("Правила прогресса"));
    dialog.resize(560, 620); dialog.setMinimumSize(480, 500);
    auto* outer = new QVBoxLayout(&dialog);
    auto* hint = new QLabel(QString::fromUtf8("Изменения влияют на будущие расчёты XP. Накопленный прогресс профилей автоматически не пересчитывается."));
    hint->setWordWrap(true); outer->addWidget(hint);
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); outer->addWidget(scroll, 1);
    auto* content = new QWidget; auto* form = new QFormLayout(content); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const auto current = workspace.data.rulesConfig;
    auto* levelBase = integerField(content, "rulesLevelBase", current.levelBaseXp, 1);
    auto* levelLinear = integerField(content, "rulesLevelLinear", current.levelLinearXp);
    auto* levelQuadratic = integerField(content, "rulesLevelQuadratic", current.levelQuadraticXp);
    form->addRow(QString::fromUtf8("Базовый XP уровня"), levelBase);
    form->addRow(QString::fromUtf8("Линейный прирост"), levelLinear);
    form->addRow(QString::fromUtf8("Квадратичный прирост"), levelQuadratic);
    std::array<QSpinBox*, Profile::kCategoryCount> categories{};
    for (size_t i = 0; i < categories.size(); ++i) {
        categories[i] = integerField(content, ("rulesCategory" + std::to_string(i)).c_str(), current.categoryBaseXp[i]);
        form->addRow(QString::fromUtf8("Категория %1 · базовый XP").arg(QString::fromUtf8(Profile::kCategoryLabels[i])), categories[i]);
    }
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
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить")); buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена")); outer->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) { notice->setText(QString::fromUtf8("Сначала завершите восстановление данных.")); return; }
        GameplayConfig draft;
        draft.levelBaseXp = levelBase->value(); draft.levelLinearXp = levelLinear->value(); draft.levelQuadraticXp = levelQuadratic->value();
        for (size_t i = 0; i < categories.size(); ++i) draft.categoryBaseXp[i] = categories[i]->value();
        draft.focusBaseBonus = float(focusBase->value()); draft.focusAdditionalBonus = float(focusExtra->value());
        draft.repeatRewardFactor = float(repeat->value()); draft.recoveryRewardFactor = float(recovery->value()); draft.recoveryWarmupTasks = warmup->value();
        draft = SanitizeGameplayConfig(draft);
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
        workspace.data.rulesConfig = draft; SetGameplayConfig(draft); dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
