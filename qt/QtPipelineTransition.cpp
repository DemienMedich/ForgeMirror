#include "QtPipelineTransition.h"
#include "AppTaskCompletionService.h"
#include "QtScrollableDialog.h"
#include <QtWidgets>
#include <algorithm>
#include <cmath>
#include <set>

namespace {
QString q(const std::string& value) { return QString::fromUtf8(value.data(), int(value.size())); }

// Reserve the label's actual styled wrapped height after width/font changes.
// Long decision context must not be compressed inside the scrollable body.
class TransitionWrappedLabel final : public QLabel {
public:
    using QLabel::QLabel;
    void setText(const QString& value) {
        QLabel::setText(value);
        setAccessibleDescription(value);
        setVisible(!value.isEmpty());
        reserveTextHeight();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        reserveTextHeight();
    }
    void showEvent(QShowEvent* event) override {
        QLabel::showEvent(event);
        reserveTextHeight();
    }
    void changeEvent(QEvent* event) override {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
            reserveTextHeight();
    }

private:
    void reserveTextHeight() {
        if (!isVisible() || width() <= 0) return;
        const int wanted = std::max(0, QLabel::heightForWidth(width()));
        if (minimumHeight() != wanted) setMinimumHeight(wanted);
    }
};

class TransitionDetails final : public QTextBrowser {
public:
    using QTextBrowser::QTextBrowser;
    QSize sizeHint() const override {
        return QSize(QTextBrowser::sizeHint().width(), minimumHeight());
    }
};
}

bool ShowPipelineTransition(QWidget* parent, QtWorkspace& workspace, const std::string& taskId) {
    const auto task = std::find_if(workspace.data.tasks.begin(), workspace.data.tasks.end(),
        [&](const auto& t) { return t.id == taskId; });
    if (task == workspace.data.tasks.end()) return false;
    const auto sourceId = task->pipelineStepId;
    const auto source = std::find_if(workspace.data.pipelineSteps.begin(), workspace.data.pipelineSteps.end(),
        [&](const auto& s) { return !sourceId.empty() && s.id == sourceId; });
    QtScrollableDialog dialog(parent, QSize(640, 520), QSize(420, 300));
    dialog.setObjectName("pipelineTransition");
    dialog.bodyWidget()->setProperty("useMeasuredFormHeight", true);
    dialog.setWindowTitle(QString::fromUtf8("Следующий этап"));
    dialog.setAccessibleName(QString::fromUtf8("Переход задачи на следующий этап"));
    auto* form = dialog.formLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(dialog.scaledMetric(8));
    form->setVerticalSpacing(dialog.scaledMetric(8));
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setSizeConstraint(QLayout::SetMinimumSize);
    dialog.footerLayout()->setSpacing(dialog.scaledMetric(8));
    auto label = [&](const char* name, const QString& accessibleName, const QString& value) {
        auto* result = new TransitionWrappedLabel(dialog.bodyWidget());
        result->setObjectName(QString::fromLatin1(name));
        result->setTextFormat(Qt::PlainText);
        result->setWordWrap(true);
        auto policy = QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        result->setSizePolicy(policy);
        result->setAccessibleName(accessibleName);
        result->setText(value);
        form->addRow(result);
        return result;
    };
    auto* title = label("transitionTaskTitle", QString::fromUtf8("Задача для перехода"), q(task->title));
    QFont titleFont = title->font();
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    label("transitionCurrentStage", QString::fromUtf8("Текущий этап задачи"),
        QString::fromUtf8("Сейчас: ") + q(source == workspace.data.pipelineSteps.end() ? task->pipelineStep : source->title));
    auto* choices = new QComboBox(dialog.bodyWidget());
    choices->setObjectName("nextStage");
    choices->setAccessibleName(QString::fromUtf8("Следующий этап задачи"));
    choices->setAccessibleDescription(QString::fromUtf8("Выберите доступный этап из настроенных связей текущего этапа. Полное название выбранного этапа показано ниже."));
    choices->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    choices->setMinimumContentsLength(1);
    choices->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    std::set<std::string> seen;
    if (source != workspace.data.pipelineSteps.end() && task->status != 2) {
        for (const auto& id : source->nextIds) {
            if (id == sourceId || !seen.insert(id).second) continue;
            const auto found = std::find_if(workspace.data.pipelineSteps.begin(), workspace.data.pipelineSteps.end(),
                [&](const auto& s) { return s.id == id; });
            if (found != workspace.data.pipelineSteps.end()) choices->addItem(q(found->stageCode + " · " + found->title), q(id));
        }
    }
    for (int index = 0; index < choices->count(); ++index) {
        choices->setItemData(index, choices->itemText(index), Qt::ToolTipRole);
        choices->setItemData(index, choices->itemText(index), Qt::AccessibleTextRole);
    }
    form->addRow(choices);
    auto* selected = label("transitionSelectedStage", QString::fromUtf8("Выбранный следующий этап"), QString());
    auto* details = new TransitionDetails(dialog.bodyWidget());
    details->setObjectName("transitionDetails");
    details->setAccessibleName(QString::fromUtf8("Критерий готовности и требования следующего этапа"));
    details->setOpenExternalLinks(false);
    details->setTabChangesFocus(true);
    details->setLineWrapMode(QTextEdit::WidgetWidth);
    QTextOption detailOption = details->document()->defaultTextOption();
    detailOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    details->document()->setDefaultTextOption(detailOption);
    details->ensurePolished();
    const int documentMargins = int(std::ceil(details->document()->documentMargin() * 2.0));
    details->setMinimumHeight(QFontMetrics(details->font()).lineSpacing() * 3 +
        documentMargins + details->frameWidth() * 2 + dialog.scaledMetric(8));
    details->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    form->addRow(details);
    auto update = [&] {
        const auto id = choices->currentData().toString().toUtf8().toStdString();
        const auto target = std::find_if(workspace.data.pipelineSteps.begin(), workspace.data.pipelineSteps.end(),
            [&](const auto& s) { return s.id == id; });
        QString text;
        if (source != workspace.data.pipelineSteps.end()) text += QString::fromUtf8("Готовность текущего этапа:\n") + q(source->doneCriteria) + "\n\n";
        if (target != workspace.data.pipelineSteps.end()) text += QString::fromUtf8("На следующем этапе:\n") + q(target->description) +
            QString::fromUtf8("\n\nВход:\n") + q(target->input) + QString::fromUtf8("\n\nОтветственный: ") + q(target->owner);
        details->setPlainText(text);
        details->setAccessibleDescription(text);
        selected->setText(target == workspace.data.pipelineSteps.end() ? QString() :
            QString::fromUtf8("Следующий: ") + choices->currentText());
        choices->setToolTip(choices->currentText());
    };
    QObject::connect(choices, &QComboBox::currentIndexChanged, &dialog, update);
    update();
    auto* ready = new QCheckBox(QString::fromUtf8("Готов к переходу"), dialog.bodyWidget());
    ready->setObjectName("stageReady");
    ready->setAccessibleName(QString::fromUtf8("Подтверждаю готовность к следующему этапу"));
    ready->setAccessibleDescription(QString::fromUtf8("Подтвердите выполнение критериев текущего этапа. При выборе другого следующего этапа подтверждение сбрасывается."));
    ready->setToolTip(ready->accessibleDescription());
    form->addRow(ready);
    label("transitionReadyHint", QString::fromUtf8("Условие перехода"),
        QString::fromUtf8("Подтверждаю готовность к следующему этапу. Критерий текущего этапа указан выше."));
    QObject::connect(choices, &QComboBox::currentIndexChanged, ready, [ready] { ready->setChecked(false); });
    auto* notice = new TransitionWrappedLabel(&dialog);
    notice->setObjectName("transitionNotice");
    notice->setTextFormat(Qt::PlainText);
    notice->setWordWrap(true);
    notice->setAccessibleName(QString::fromUtf8("Результат перехода задачи"));
    auto noticePolicy = QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    noticePolicy.setHeightForWidth(true);
    notice->setSizePolicy(noticePolicy);
    notice->setText(QString());
    if (!choices->count()) notice->setText(QString::fromUtf8(task->status == 2 ?
        "Задача завершена. Для перехода сначала измените её статус." :
        "Нет доступных переходов. Проверьте связи этапа; начальный этап задаётся в редакторе задачи."));
    dialog.footerLayout()->addWidget(notice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName("transitionButtons");
    auto* save = buttons->button(QDialogButtonBox::Save);
    auto* cancel = buttons->button(QDialogButtonBox::Cancel);
    save->setObjectName("transitionSave");
    save->setText(QString::fromUtf8("Перейти"));
    save->setAccessibleName(QString::fromUtf8("Перевести задачу на выбранный следующий этап"));
    save->setProperty("primary", true);
    save->setMinimumHeight(dialog.scaledMetric(32));
    save->setAutoDefault(true);
    save->setDefault(true);
    save->setEnabled(false);
    cancel->setObjectName("transitionCancel");
    cancel->setText(QString::fromUtf8("Отмена"));
    cancel->setAccessibleName(QString::fromUtf8("Отменить переход без изменения задачи"));
    cancel->setAutoDefault(false);
    dialog.footerLayout()->addWidget(buttons);
    QWidget::setTabOrder(choices, details);
    QWidget::setTabOrder(details, ready);
    QWidget::setTabOrder(ready, save);
    QWidget::setTabOrder(save, cancel);
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
