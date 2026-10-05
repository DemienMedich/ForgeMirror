#pragma once

#include "QtActionIcons.h"
#include "QtScrollableDialog.h"
#include <QtWidgets>
#include <algorithm>

// Presentation only, for the existing widget-based file/folder pickers. Native
// pickers, file models, filters, overwrite checks and export handlers stay owned
// by Qt/the caller. Child names below are covered by the actual-dialog tests.
inline void PrepareQtFilePicker(QFileDialog& dialog) {
    if (!dialog.testOption(QFileDialog::DontUseNativeDialog)) return;
    const bool saving = dialog.acceptMode() == QFileDialog::AcceptSave;
    const bool directory = dialog.fileMode() == QFileDialog::Directory;
    dialog.setAccessibleName(dialog.windowTitle());
    dialog.setLabelText(QFileDialog::LookIn, QString::fromUtf8("Папка:"));
    dialog.setLabelText(QFileDialog::FileName, QString::fromUtf8(directory ? "Папка:" : "Имя файла:"));
    dialog.setLabelText(QFileDialog::FileType, QString::fromUtf8("Формат:"));
    dialog.setLabelText(QFileDialog::Accept, QString::fromUtf8(saving ? "Сохранить" : directory ? "Выбрать" : "Открыть"));
    dialog.setLabelText(QFileDialog::Reject, QString::fromUtf8("Отмена"));
    auto name = [&](const char* object, const char* label) {
        if (auto* widget = dialog.findChild<QWidget*>(QLatin1String(object)))
            widget->setAccessibleName(QString::fromUtf8(label));
    };
    name("lookInCombo", saving ? "Папка сохранения" : "Текущая папка");
    name("fileNameEdit", saving ? "Имя сохраняемого файла" : directory ? "Выбранная папка" : "Имя выбираемого файла");
    name("fileTypeCombo", saving ? "Формат сохраняемого файла" : "Тип выбираемого объекта");
    name("sidebar", "Избранные папки");
    name("listView", "Файлы и папки");
    name("treeView", "Подробный список файлов и папок");

    const double base = qApp->property("forgeBasePointSize").toDouble();
    const double scale = base > 0.0
        ? std::clamp(dialog.font().pointSizeF() / base, 0.9, 2.0) : 1.0;
    const int iconSize = qRound(16 * scale);
    auto tool = [&](const char* object, const char* label, QtActionIcon icon, qreal rotation = 0) {
        if (auto* button = dialog.findChild<QToolButton*>(QLatin1String(object))) {
            const auto text = QString::fromUtf8(label);
            button->setAccessibleName(text);
            button->setAccessibleDescription(text);
            button->setToolTip(text);
            button->setIcon(CreateQtActionIcon(icon, button->palette(), rotation));
            button->setIconSize(QSize(iconSize, iconSize));
        }
    };
    tool("backButton", "Назад", QtActionIcon::MoveUp, -90);
    tool("forwardButton", "Вперёд", QtActionIcon::MoveUp, 90);
    tool("toParentButton", "На уровень выше", QtActionIcon::MoveUp);
    tool("newFolderButton", "Создать папку", QtActionIcon::NewFolder);
    tool("listModeButton", "Список", QtActionIcon::List);
    tool("detailModeButton", "Таблица", QtActionIcon::Table);

    // Keep the location usable at large text sizes: the toolbar may move below
    // it, rather than squeezing the current folder down to an arrow alone.
    auto* grid = qobject_cast<QGridLayout*>(dialog.layout());
    auto* folder = dialog.findChild<QComboBox*>("lookInCombo");
    auto* label = dialog.findChild<QLabel*>("lookInLabel");
    auto* item = grid ? grid->itemAtPosition(0, 1) : nullptr;
    auto* oldRow = item ? item->layout() : nullptr;
    QList<QToolButton*> actions;
    for (const char* object : {"backButton", "forwardButton", "toParentButton", "newFolderButton", "listModeButton", "detailModeButton"})
        if (auto* button = dialog.findChild<QToolButton*>(QLatin1String(object))) actions.append(button);
    if (grid && folder && label && oldRow && actions.size() == 6) {
        auto* header = new QtDialogAdaptiveRow(&dialog, qRound(8 * scale));
        header->setObjectName("exportPickerHeader");
        auto* location = new QWidget(header);
        auto* locationRow = new QHBoxLayout(location);
        locationRow->setContentsMargins(0, 0, 0, 0);
        locationRow->setSpacing(qRound(8 * scale));
        grid->removeWidget(label);
        locationRow->addWidget(label);
        folder->setMinimumContentsLength(16);
        folder->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        folder->setMinimumWidth(qRound(140 * scale));
        folder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        locationRow->addWidget(folder, 1);
        auto* toolbar = new QWidget(header);
        auto* toolbarRow = new QHBoxLayout(toolbar);
        toolbarRow->setContentsMargins(0, 0, 0, 0);
        toolbarRow->setSpacing(qRound(4 * scale));
        toolbarRow->addStretch();
        for (auto* button : actions) toolbarRow->addWidget(button);
        header->addWidget(location, 1);
        header->addWidget(toolbar);
        grid->removeItem(oldRow);
        delete oldRow;
        grid->addWidget(header, 0, 0, 1, 3);
    }
    // Reparenting reapplies the global stylesheet's minimum height. Measure
    // only after the final hierarchy is installed (90% used to give 25 < 28).
    for (auto* button : actions) {
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->setMinimumSize(button->sizeHint().expandedTo(
            QSize(qRound(26 * scale), qRound(26 * scale))));
    }

    if (auto* box = dialog.findChild<QDialogButtonBox*>()) {
        const auto acceptRole = saving ? QDialogButtonBox::Save : QDialogButtonBox::Open;
        for (auto role : {acceptRole, QDialogButtonBox::Cancel}) {
            if (auto* button = box->button(role)) {
                button->setAccessibleName(button->text().remove('&'));
                button->setProperty("primary", role == acceptRole);
                button->style()->unpolish(button);
                button->style()->polish(button);
                button->setMinimumSize(button->sizeHint().expandedTo(
                    QSize(0, qRound((role == acceptRole ? 32 : 26) * scale))));
            }
        }
    }
}

inline void PrepareQtExportPicker(QFileDialog& dialog) { PrepareQtFilePicker(dialog); }
