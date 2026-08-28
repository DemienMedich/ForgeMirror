#include "QtPipelineTransition.h"
#include "AppTaskCompletionService.h"
#include <QtWidgets>
#include <algorithm>
#include <set>

namespace {
QString q(const std::string& value) { return QString::fromUtf8(value.data(), int(value.size())); }
}
bool ShowPipelineTransition(QWidget* parent, QtWorkspace& workspace, const std::string& taskId) {
    const auto task = std::find_if(workspace.data.tasks.begin(), workspace.data.tasks.end(),
        [&](const auto& t) { return t.id == taskId; });
    if (task == workspace.data.tasks.end()) return false;
    const auto sourceId = task->pipelineStepId;
    const auto source = std::find_if(workspace.data.pipelineSteps.begin(), workspace.data.pipelineSteps.end(),
        [&](const auto& s) { return !sourceId.empty() && s.id == sourceId; });
    QDialog dialog(parent);
    dialog.setObjectName("pipelineTransition");
    dialog.setWindowTitle(QString::fromUtf8("Следующий этап"));
    dialog.resize(560, 420);
    dialog.setMinimumSize(480, 360);
    auto* layout = new QVBoxLayout(&dialog);
    auto* title = new QLabel(q(task->title));
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    layout->addWidget(title);
    auto* current = new QLabel(QString::fromUtf8("Сейчас: ") + q(source == workspace.data.pipelineSteps.end() ? task->pipelineStep : source->title));
    current->setTextFormat(Qt::PlainText);
    current->setWordWrap(true);
    layout->addWidget(current);
    auto* choices = new QComboBox;
    choices->setObjectName("nextStage");
    std::set<std::string> seen;
    if (source != workspace.data.pipelineSteps.end() && task->status != 2) {
        for (const auto& id : source->nextIds) {
            if (id == sourceId || !seen.insert(id).second) continue;
            const auto found = std::find_if(workspace.data.pipelineSteps.begin(), workspace.data.pipelineSteps.end(),
                [&](const auto& s) { return s.id == id; });
            if (found != workspace.data.pipelineSteps.end()) choices->addItem(q(found->stageCode + " · " + found->title), q(id));
        }
    }
    layout->addWidget(choices);
    auto* details = new QTextBrowser;
    details->setObjectName("transitionDetails");
    details->setOpenExternalLinks(false);
    layout->addWidget(details, 1);
    auto update = [&] {
        const auto id = choices->currentData().toString().toUtf8().toStdString();
        const auto target = std::find_if(workspace.data.pipelineSteps.begin(), workspace.data.pipelineSteps.end(),
            [&](const auto& s) { return s.id == id; });
        QString text;
        if (source != workspace.data.pipelineSteps.end()) text += QString::fromUtf8("Готовность текущего этапа:\n") + q(source->doneCriteria) + "\n\n";
        if (target != workspace.data.pipelineSteps.end()) text += QString::fromUtf8("На следующем этапе:\n") + q(target->description) +
            QString::fromUtf8("\n\nВход:\n") + q(target->input) + QString::fromUtf8("\n\nОтветственный: ") + q(target->owner);
        details->setPlainText(text);
    };
    QObject::connect(choices, &QComboBox::currentIndexChanged, &dialog, update);
    update();
    auto* ready = new QCheckBox(QString::fromUtf8("Подтверждаю готовность к следующему этапу"));
    ready->setObjectName("stageReady");
    layout->addWidget(ready);
    QObject::connect(choices, &QComboBox::currentIndexChanged, ready, [ready] { ready->setChecked(false); });
    auto* notice = new QLabel;
    notice->setObjectName("transitionNotice");
    notice->setTextFormat(Qt::PlainText);
    notice->setWordWrap(true);
    if (!choices->count()) notice->setText(QString::fromUtf8(task->status == 2 ?
        "Задача завершена. Для перехода сначала измените её статус." :
        "Нет доступных переходов. Проверьте связи этапа; начальный этап задаётся в редакторе задачи."));
    layout->addWidget(notice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto* save = buttons->button(QDialogButtonBox::Save);
    save->setText(QString::fromUtf8("Перейти"));
    save->setProperty("primary", true);
    save->setEnabled(false);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    layout->addWidget(buttons);
    QObject::connect(ready, &QCheckBox::toggled, &dialog, [&](bool checked) { save->setEnabled(checked && choices->count() > 0); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (!ready->isChecked() || choices->currentIndex() < 0) return;
        const auto result = AdvanceTaskPipeline(workspace.directory, workspace.data.tasks, workspace.data.taskAudit,
            workspace.data.pipelineSteps, taskId, sourceId, choices->currentData().toString().toUtf8().toStdString(), "admin/qt");
        if (!result.ok) { notice->setText(q(result.errorMessage)); return; }
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
