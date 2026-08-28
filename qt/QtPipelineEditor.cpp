#include "QtPipelineEditor.h"
#include "AppPipelineService.h"
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
    QDialog dialog(parent);
    dialog.setObjectName("pipelineEditor");
    dialog.setWindowTitle(QString::fromUtf8(id.empty() ? "Новый этап" : "Редактирование этапа"));
    dialog.resize(640, 520);
    dialog.setMinimumSize(520, 440);
    auto* layout = new QVBoxLayout(&dialog);
    auto* tabs = new QTabWidget;
    tabs->setObjectName("pipelineTabs");
    layout->addWidget(tabs, 1);
    auto page = [&](const char* title) {
        auto* widget = new QWidget;
        auto* form = new QFormLayout(widget);
        tabs->addTab(widget, QString::fromUtf8(title));
        return form;
    };
    auto* basic = page("Этап");
    auto* criteria = page("Готовность");
    auto* links = page("Переходы");
    auto* notes = page("Примечания");
    std::map<std::string, QLineEdit*> lines;
    std::map<std::string, QPlainTextEdit*> texts;
    auto line = [&](QFormLayout* form, const char* key, const char* label, const std::string& value) {
        auto* input = new QLineEdit(q(value));
        input->setObjectName(key);
        form->addRow(QString::fromUtf8(label), input);
        lines[key] = input;
    };
    auto text = [&](QFormLayout* form, const char* key, const char* label, const std::string& value) {
        auto* input = new QPlainTextEdit(q(value));
        input->setObjectName(key);
        input->setMinimumHeight(56);
        input->setMaximumHeight(96);
        form->addRow(QString::fromUtf8(label), input);
        texts[key] = input;
    };
    line(basic, "stageTitle", "Название", original.title);
    line(basic, "stageCode", "Код этапа", original.stageCode);
    line(basic, "stageBranch", "Ветка", original.branch);
    line(basic, "stageOwner", "Ответственный", original.owner);
    text(basic, "stageDescription", "Описание", original.description);
    text(criteria, "stageInput", "Вход", original.input);
    text(criteria, "stageOutput", "Выход", original.output);
    text(criteria, "stageDone", "Готово, когда", original.doneCriteria);
    text(criteria, "stageEngine", "Проверка движка", original.engineCheck);
    text(notes, "stageRisk", "Риски", original.risk);
    text(notes, "stageLegacy", "Заметки", original.legacyNotes);
    QStringList hintLines;
    for (const auto& hint : original.hints) hintLines << q(hint);
    text(notes, "stageHints", "Подсказки", u(hintLines.join('\n')));
    line(links, "stageNextLabel", "Следующий шаг", original.nextStageLabel);
    auto* next = new QListWidget;
    next->setObjectName("stageNextIds");
    links->addRow(next);
    auto addLink = [&](const std::string& target, const QString& title, bool selected) {
        auto* item = new QListWidgetItem(title, next);
        item->setData(Qt::UserRole, q(target));
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
    hint->setWordWrap(true);
    links->addRow(hint);
    auto* notice = new QLabel;
    notice->setObjectName("pipelineNotice");
    notice->setTextFormat(Qt::PlainText);
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) {
            notice->setText(QString::fromUtf8("Сначала завершите восстановление данных через обновление.")); return;
        }
        PipelineStep draft = original;
        draft.title = u(lines["stageTitle"]->text().trimmed());
        if (draft.title.empty()) { notice->setText(QString::fromUtf8("Название этапа обязательно.")); tabs->setCurrentIndex(0); return; }
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
        auto candidate = workspace.data.pipelineSteps;
        if (id.empty()) {
            draft.id = "qt-step-" + u(QUuid::createUuid().toString(QUuid::WithoutBraces));
            candidate.push_back(draft);
        } else {
            const auto current = std::find_if(candidate.begin(), candidate.end(), [&](const auto& step) { return step.id == id; });
            if (current == candidate.end()) { notice->setText(QString::fromUtf8("Этап больше не существует.")); return; }
            *current = draft;
        }
        // Persist the complete draft once; cancellation never creates placeholder steps.
        if (!AppSavePipelineData(workspace.directory, candidate)) {
            notice->setText(QString::fromUtf8("Не удалось сохранить пайплайн. Исходные данные не изменены.")); return;
        }
        workspace.data.pipelineSteps = std::move(candidate);
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
