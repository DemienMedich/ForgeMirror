#include "QtTaskCompletionDialog.h"
#include "QtWorkspace.h"
#include "AppTaskCompletionService.h"
#include <QtWidgets>
#include <algorithm>

namespace {
QString q(const std::string& s) { return QString::fromUtf8(s.data(), int(s.size())); }
std::string u(const QString& s) { return s.toUtf8().toStdString(); }
void setupTable(QTableWidget* table, const QStringList& headers) {
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(32);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
}
}

bool ShowTaskCompletionDialog(QWidget* parent, QtWorkspace& workspace,
                              const QString& taskId, const QString& activeProfileId) {
    const auto it = std::find_if(workspace.data.tasks.begin(), workspace.data.tasks.end(),
        [&](const auto& task) { return task.id == u(taskId); });
    if (it == workspace.data.tasks.end() || !it->participants.empty()) return false;
    const auto task = *it;
    AppContext context{workspace.directory, *workspace.storage, workspace.catalog};
    QDialog dialog(parent);
    dialog.setObjectName("taskCompletionDialog");
    dialog.setWindowTitle(QString::fromUtf8("Завершение задачи и XP"));
    dialog.resize(800, 640);
    dialog.setMinimumSize(640, 480);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);
    auto* title = new QLabel(q(task.title));
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    layout->addWidget(title);
    auto* top = new QHBoxLayout;
    auto* category = new QComboBox;
    category->setObjectName("xpCategory");
    for (auto* label : Profile::kCategoryLabels) category->addItem(label);
    category->setCurrentIndex(std::clamp(task.category, 0, 4));
    auto* score = new QSpinBox;
    score->setObjectName("xpScore");
    score->setRange(1, 10);
    score->setValue(10);
    top->addWidget(new QLabel(QString::fromUtf8("Категория")));
    top->addWidget(category);
    top->addWidget(new QLabel(QString::fromUtf8("Оценка")));
    top->addWidget(score);
    top->addStretch();
    auto* penalty = new QLabel(QString::fromUtf8("Штраф задачи: %1% ").arg(task.deadlinePenaltyPercent));
    penalty->setToolTip(QString::fromUtf8("Берётся заданный в задаче штраф, как в ImGui. Снижает общий пул до распределения."));
    top->addWidget(penalty);
    layout->addLayout(top);
    auto* participantsHeader = new QHBoxLayout;
    participantsHeader->addWidget(new QLabel(QString::fromUtf8("Участники · вклад должен давать 100% · 0% исключает участника")));
    participantsHeader->addStretch();
    auto* even = new QPushButton(QString::fromUtf8("Поровну"));
    even->setToolTip(QString::fromUtf8("Разделить 100% между участниками, у которых вклад больше нуля."));
    participantsHeader->addWidget(even);
    layout->addLayout(participantsHeader);
    auto* participants = new QTableWidget;
    participants->setObjectName("xpParticipants");
    setupTable(participants, {QString::fromUtf8("Профиль"), QString::fromUtf8("Вклад, %"), "XP", QString::fromUtf8("XP навыков"), QString::fromUtf8("Модификаторы")});
    layout->addWidget(participants, 1);
    std::vector<std::string> profileIds;
    std::vector<QSpinBox*> shares;
    for (const auto& profile : workspace.profiles) if (!profile.archived) {
        int row = participants->rowCount();
        participants->insertRow(row);
        participants->setItem(row, 0, new QTableWidgetItem(q(profile.name)));
        auto* share = new QSpinBox;
        share->setRange(0, 100);
        const bool assigned = std::find(task.assignees.begin(), task.assignees.end(), profile.id) != task.assignees.end();
        share->setValue(assigned || (task.assignees.empty() && profile.id == u(activeProfileId)) ? 1 : 0);
        participants->setCellWidget(row, 1, share);
        for (int c = 2; c < 5; ++c) participants->setItem(row, c, new QTableWidgetItem);
        shares.push_back(share);
        profileIds.push_back(profile.id);
    }
    auto split = [&] {
        int count = int(std::count_if(shares.begin(), shares.end(), [](auto* spin) { return spin->value() > 0; }));
        if (!count) return;
        int remainder = 100 % count;
        for (auto* spin : shares) if (spin->value() > 0) {
            QSignalBlocker blocker(spin);
            spin->setValue(100 / count + (remainder-- > 0 ? 1 : 0));
        }
    };
    split();
    layout->addWidget(new QLabel(QString::fromUtf8("Навыки · оценки 0–5 автоматически распределяют 100%")));
    auto* skills = new QTableWidget;
    skills->setObjectName("xpSkills");
    setupTable(skills, {QString::fromUtf8("Навык"), QString::fromUtf8("Оценка"), QString::fromUtf8("Доля, %")});
    layout->addWidget(skills, 1);
    std::vector<std::string> skillIds = workspace.catalog.skills();
    std::vector<QSpinBox*> ratings;
    for (const auto& id : skillIds) {
        int row = skills->rowCount();
        skills->insertRow(row);
        skills->setItem(row, 0, new QTableWidgetItem(q(workspace.catalog.display_name(id))));
        auto* rating = new QSpinBox;
        rating->setRange(0, 5);
        rating->setValue(task.skillIds.empty() || std::find(task.skillIds.begin(), task.skillIds.end(), id) != task.skillIds.end() ? 1 : 0);
        skills->setCellWidget(row, 1, rating);
        skills->setItem(row, 2, new QTableWidgetItem);
        ratings.push_back(rating);
    }
    auto* summary = new QLabel;
    summary->setObjectName("xpSummary");
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    summary->setToolTip(QString::fromUtf8("Пул = XP категории × множитель оценки × фокус × штраф задачи.\n"
        "Повтор и прогрев снижают общий XP. Бонусы достижений действуют на навыки; дух — на оба вида XP."));
    layout->addWidget(summary);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto* save = buttons->button(QDialogButtonBox::Save);
    save->setText(QString::fromUtf8("Завершить и начислить XP"));
    save->setObjectName("completeXp");
    save->setProperty("primary", true);
    save->setFixedHeight(32);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    layout->addWidget(buttons);
    auto input = [&] {
        TaskCompletionInput request;
        request.taskId = task.id;
        request.category = category->currentIndex();
        request.score = score->value();
        request.now = QDateTime::currentSecsSinceEpoch();
        request.restoreProfileId = u(activeProfileId);
        request.actor = "admin/qt";
        for (size_t i = 0; i < shares.size(); ++i) if (shares[i]->value() > 0)
            request.shares.push_back({profileIds[i], shares[i]->value()});
        for (size_t i = 0; i < ratings.size(); ++i) request.skills.push_back({skillIds[i], ratings[i]->value()});
        return request;
    };
    auto refresh = [&] {
        const auto preview = PreviewTaskCompletion(context, workspace.data.tasks, input());
        save->setEnabled(preview.ok);
        for (int r = 0; r < participants->rowCount(); ++r)
            for (int c = 2; c < 5; ++c) participants->item(r, c)->setText(QString::fromUtf8("—"));
        for (int r = 0; r < skills->rowCount(); ++r) skills->item(r, 2)->setText(preview.ok ? QString::number(preview.skillPercents[size_t(r)]) : QString::fromUtf8("—"));
        if (!preview.ok) { summary->setText(q(preview.errorMessage)); return; }
        for (size_t i = 0; i < preview.finalize.participants.size(); ++i) {
            const auto& p = preview.finalize.participants[i];
            const auto found = std::find(profileIds.begin(), profileIds.end(), p.profileId);
            const int row = int(found - profileIds.begin());
            participants->item(row, 2)->setText(QString::number(p.globalXp));
            participants->item(row, 3)->setText(QString::number(p.skillXp));
            participants->item(row, 4)->setText(q(preview.modifiers[i]));
        }
        summary->setText(QString::fromUtf8("Пул до штрафа: %1 XP  ·  к распределению: %2 XP  ·  участников: %3\n"
            "Сохранение закроет задачу и запишет XP всем участникам. Повторное начисление запрещено.")
            .arg(preview.rawPool).arg(preview.finalize.basePool).arg(preview.finalize.participants.size()));
    };
    for (auto* spin : shares) QObject::connect(spin, &QSpinBox::valueChanged, &dialog, refresh);
    for (auto* spin : ratings) QObject::connect(spin, &QSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(score, &QSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(category, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(even, &QPushButton::clicked, &dialog, [&] { split(); refresh(); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        save->setEnabled(false);
        const auto result = CompleteTaskWithXp(context, workspace.data.tasks, workspace.data.taskAudit, input());
        if (!result.ok) {
            summary->setText(q(result.errorMessage));
            // A pending rollback must be resolved before any further mutations.
            save->setEnabled(!std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"));
            return;
        }
        dialog.accept();
    });
    refresh();
    return dialog.exec() == QDialog::Accepted;
}
