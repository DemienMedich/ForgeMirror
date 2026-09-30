#include "QtPipelineEditor.h"
#include "AppPipelineService.h"
#include "QtScrollableDialog.h"
#include <QtWidgets>
#include <algorithm>
#include <map>

namespace {
QString q(const std::string& s) { return QString::fromUtf8(s.data(), int(s.size())); }
std::string u(const QString& s) { return s.toUtf8().toStdString(); }
}

bool ShowPipelineEditor(QWidget* parent, QtWorkspace& workspace, const std::string& id) {
    const auto found = std::find_if(workspace.data.pipelineSteps.begin(), workspace.data.pipelineSteps.end(),
        [&](const auto& step) { return step.id == id; });
    if (!id.empty() && found == workspace.data.pipelineSteps.end()) return false;
    const PipelineStep original = id.empty() ? PipelineStep{} : *found;
    const auto expectedSnapshot = workspace.data.pipelineSteps;
    QtScrollableDialog dialog(parent, QSize(640, 520), QSize(520, 440));
    dialog.setObjectName("pipelineEditor");
    dialog.setWindowTitle(QString::fromUtf8(id.empty() ? "Новый этап" : "Редактирование этапа"));
    auto* layout = qobject_cast<QVBoxLayout*>(dialog.layout());
    auto* tabs = new QTabWidget;
    tabs->setObjectName("pipelineTabs");
    tabs->setAccessibleName(QString::fromUtf8("Разделы редактора этапа пайплайна"));
    tabs->setUsesScrollButtons(true);
    // Reuse the helper's scroll area as the first tab: no nested outer scroll,
    // while its persistent footer and screen bounds still belong to the dialog.
    layout->removeWidget(dialog.scrollArea());
    layout->insertWidget(0, tabs, 1);
    auto page = [&](const char* key, const char* title, const char* description, bool first = false) {
        auto* scroll = first ? dialog.scrollArea() : new QScrollArea;
        auto* widget = first ? dialog.bodyWidget() : new QWidget;
        auto* form = first ? dialog.formLayout() : new QFormLayout(widget);
        scroll->setObjectName(QString::fromLatin1("pipeline%1ScrollArea").arg(QString::fromLatin1(key)));
        scroll->setAccessibleName(QString::fromUtf8(description));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        widget->setObjectName(QString::fromLatin1("pipeline%1Content").arg(QString::fromLatin1(key)));
        widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        form->setContentsMargins(dialog.scaledMetric(8), dialog.scaledMetric(8), dialog.scaledMetric(8), dialog.scaledMetric(8));
        form->setHorizontalSpacing(dialog.scaledMetric(8));
        form->setVerticalSpacing(dialog.scaledMetric(8));
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        if (!first) scroll->setWidget(widget);
        const int index = tabs->addTab(scroll, QString::fromUtf8(title));
        tabs->setTabToolTip(index, QString::fromUtf8(description));
        return form;
    };
    auto* basic = page("Basic", "Этап", "Название, код, ветка, ответственный и описание этапа", true);
    auto* criteria = page("Criteria", "Готовность", "Вход, выход, критерий готовности и проверка в движке");
    auto* links = page("Links", "Связи", "Подпись следующего шага и допустимые следующие этапы");
    auto* notes = page("Notes", "Заметки", "Риски, практика Forge Mirror и подсказки этапа");
    std::map<std::string, QLineEdit*> lines;
    std::map<std::string, QPlainTextEdit*> texts;
    auto addField = [&](QFormLayout* form, const char* key, const char* label, const char* description, QWidget* input) {
        const QString fullDescription = QString::fromUtf8(description);
        auto* caption = new QLabel(QString::fromUtf8(label));
        caption->setObjectName(QString::fromLatin1(key) + QStringLiteral("Label"));
        caption->setTextFormat(Qt::PlainText);
        caption->setWordWrap(true);
        caption->setBuddy(input);
        caption->setToolTip(fullDescription);
        input->setToolTip(fullDescription);
        input->setAccessibleName(fullDescription);
        form->addRow(caption, input);
    };
    auto line = [&](QFormLayout* form, const char* key, const char* label, const char* description, const std::string& value) {
        auto* input = new QLineEdit(q(value));
        input->setObjectName(key);
        addField(form, key, label, description, input);
        lines[key] = input;
    };
    auto text = [&](QFormLayout* form, const char* key, const char* label, const char* description, const std::string& value) {
        auto* input = new QPlainTextEdit(q(value));
        input->setObjectName(key);
        input->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        const int minimumHeight = std::max(dialog.scaledMetric(56), QFontMetrics(input->font()).lineSpacing() * 2 + dialog.scaledMetric(8));
        input->setMinimumHeight(minimumHeight);
        input->setMaximumHeight(minimumHeight);
        addField(form, key, label, description, input);
        texts[key] = input;
    };
    line(basic, "stageTitle", "Название", "Название этапа пайплайна", original.title);
    line(basic, "stageCode", "Код", "Код этапа пайплайна", original.stageCode);
    line(basic, "stageBranch", "Ветка", "Ветка пайплайна", original.branch);
    line(basic, "stageOwner", "Ответственный", "Ответственный за этап", original.owner);
    text(basic, "stageDescription", "Описание", "Описание этапа пайплайна", original.description);
    text(criteria, "stageInput", "Вход", "Входные данные этапа", original.input);
    text(criteria, "stageOutput", "Выход", "Результат этапа", original.output);
    text(criteria, "stageDone", "Готово, когда", "Критерий готовности этапа", original.doneCriteria);
    text(criteria, "stageEngine", "В движке", "Проверка результата в движке", original.engineCheck);
    text(notes, "stageRisk", "Риски", "Риски и возвраты этапа", original.risk);
    text(notes, "stageLegacy", "Практика", "Практика Forge Mirror для этапа", original.legacyNotes);
    QStringList hintLines;
    for (const auto& hint : original.hints) hintLines << q(hint);
    text(notes, "stageHints", "Подсказки", "Подсказки этапа, по одной на строку", u(hintLines.join('\n')));
    line(links, "stageNextLabel", "Следующий шаг", "Подпись следующего шага", original.nextStageLabel);
    auto* next = new QListWidget;
    next->setObjectName("stageNextIds");
    next->setAccessibleName(QString::fromUtf8("Допустимые следующие этапы"));
    next->setAccessibleDescription(QString::fromUtf8("Пробел меняет отметку текущего этапа. Недоступные связи сохраняются, пока их отметка не снята."));
    next->setWordWrap(true);
    next->setTextElideMode(Qt::ElideNone);
    next->setResizeMode(QListView::Adjust);
    next->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    next->setMaximumHeight(dialog.scaledMetric(140));
    links->addRow(next);
    auto addLink = [&](const std::string& target, const QString& title, bool selected) {
        auto* item = new QListWidgetItem(title, next);
        item->setData(Qt::UserRole, q(target));
        item->setData(Qt::AccessibleTextRole, title);
        item->setToolTip(title);
        item->setCheckState(selected ? Qt::Checked : Qt::Unchecked);
    };
    for (const auto& step : workspace.data.pipelineSteps) {
        const bool selected = std::find(original.nextIds.begin(), original.nextIds.end(), step.id) != original.nextIds.end();
        if (step.id != id || selected) addLink(step.id, q(step.stageCode + " · " + step.title), selected);
    }
    for (const auto& target : original.nextIds) {
        const bool exists = std::any_of(workspace.data.pipelineSteps.begin(), workspace.data.pipelineSteps.end(),
            [&](const auto& step) { return step.id == target; });
        if (!exists) addLink(target, q(target) + QString::fromUtf8(" · недоступен"), true);
    }
    auto* hint = new QLabel(QString::fromUtf8("Отметьте допустимые следующие этапы. Недоступные связи сохраняются, пока вы сами их не снимете."));
    hint->setObjectName("stageNextIdsHint");
    hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true);
    hint->setAccessibleName(hint->text());
    links->addRow(hint);
    auto* notice = new QLabel;
    notice->setObjectName("pipelineNotice");
    notice->setTextFormat(Qt::PlainText);
    notice->setWordWrap(true);
    dialog.footerLayout()->addWidget(notice);
    auto* stageTitle = lines["stageTitle"];
    const QString titleDescription = QString::fromUtf8("Обязательное название этапа.");
    const QString titleError = QString::fromUtf8("Название этапа обязательно.");
    stageTitle->setAccessibleName(QString::fromUtf8("Название этапа"));
    stageTitle->setAccessibleDescription(titleDescription);
    QObject::connect(stageTitle, &QLineEdit::textChanged, &dialog, [stageTitle, notice, titleDescription, titleError](const QString& value) {
        if (!stageTitle->property("validationFailed").toBool() || value.trimmed().isEmpty()) return;
        stageTitle->setProperty("validationFailed", false);
        stageTitle->setAccessibleDescription(titleDescription);
        if (notice->text() == titleError) notice->clear();
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName("pipelineEditorButtons");
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    auto* save = buttons->button(QDialogButtonBox::Save);
    auto* cancel = buttons->button(QDialogButtonBox::Cancel);
    save->setObjectName("pipelineSave");
    cancel->setObjectName("pipelineCancel");
    save->setAccessibleName(QString::fromUtf8("Сохранить этап пайплайна"));
    cancel->setAccessibleName(QString::fromUtf8("Отменить редактирование этапа"));
    save->setProperty("primary", true);
    save->setMinimumHeight(dialog.scaledMetric(32));
    save->setAutoDefault(true);
    save->setDefault(true);
    cancel->setAutoDefault(false);
    dialog.footerLayout()->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) {
            notice->setText(QString::fromUtf8("Сначала завершите восстановление данных через обновление.")); return;
        }
        PipelineStep draft = original;
        draft.title = u(lines["stageTitle"]->text().trimmed());
        if (draft.title.empty()) {
            notice->setText(titleError);
            tabs->setCurrentIndex(0);
            stageTitle->setProperty("validationFailed", true);
            stageTitle->setAccessibleDescription(titleError);
            stageTitle->setFocus(Qt::OtherFocusReason);
            stageTitle->selectAll();
            dialog.scrollArea()->ensureWidgetVisible(stageTitle);
            return;
        }
        draft.stageCode = u(lines["stageCode"]->text());
        draft.branch = u(lines["stageBranch"]->text());
        draft.owner = u(lines["stageOwner"]->text());
        draft.nextStageLabel = u(lines["stageNextLabel"]->text());
        draft.description = u(texts["stageDescription"]->toPlainText());
        draft.input = u(texts["stageInput"]->toPlainText());
        draft.output = u(texts["stageOutput"]->toPlainText());
        draft.doneCriteria = u(texts["stageDone"]->toPlainText());
        draft.engineCheck = u(texts["stageEngine"]->toPlainText());
        draft.risk = u(texts["stageRisk"]->toPlainText());
        draft.legacyNotes = u(texts["stageLegacy"]->toPlainText());
        if (texts["stageHints"]->toPlainText() != hintLines.join('\n')) {
            draft.hints.clear();
            for (const auto& value : texts["stageHints"]->toPlainText().split('\n', Qt::SkipEmptyParts)) draft.hints.push_back(u(value));
        }
        std::vector<std::string> selected;
        for (int i = 0; i < next->count(); ++i) if (next->item(i)->checkState() == Qt::Checked)
            selected.push_back(u(next->item(i)->data(Qt::UserRole).toString()));
        draft.nextIds.clear();
        for (const auto& target : original.nextIds)
            if (std::find(selected.begin(), selected.end(), target) != selected.end()) draft.nextIds.push_back(target);
        for (const auto& target : selected)
            if (std::find(draft.nextIds.begin(), draft.nextIds.end(), target) == draft.nextIds.end()) draft.nextIds.push_back(target);
        auto candidate = expectedSnapshot;
        if (id.empty()) {
            draft.id = "qt-step-" + u(QUuid::createUuid().toString(QUuid::WithoutBraces));
            candidate.push_back(draft);
        } else {
            const auto current = std::find_if(candidate.begin(), candidate.end(), [&](const auto& step) { return step.id == id; });
            if (current == candidate.end()) { notice->setText(QString::fromUtf8("Этап больше не существует.")); return; }
            *current = draft;
        }
        // Persist the complete draft only if the editor's opening snapshot is still current.
        const auto saved = AppSavePipelineCandidate(workspace.directory, expectedSnapshot,
                                                    workspace.data.pipelineSteps, candidate);
        if (!saved.ok) {
            notice->setText(QString::fromUtf8(saved.errorMessage.empty()
                ? "Не удалось сохранить пайплайн. Исходные данные не изменены."
                : saved.errorMessage.c_str()));
            if (saved.staleSnapshot) {
                notice->setText(QString::fromUtf8("Пайплайн изменился во время редактирования. Список обновлён; закройте окно и откройте редактор снова."));
                buttons->button(QDialogButtonBox::Save)->setEnabled(false);
            }
            return;
        }
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
