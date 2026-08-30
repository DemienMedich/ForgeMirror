#include "AppTaskCompletionService.h"
#include "AppTaskWorkflowService.h"
#include "AppUtils.h"
#include "GameplayConfig.h"
#include "IJobStorage.h"
#include "SkillCatalog.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>

namespace {
struct RestoreSelection {
    IJobStorage& storage;
    std::string id;
    ~RestoreSelection() { if (!id.empty()) storage.set_active_profile(id); }
};
int checkedXp(double value) {
    if (!std::isfinite(value) || value < 0 || value > std::numeric_limits<int>::max() / 100)
        throw std::runtime_error(u8"XP выходит за безопасный диапазон. Проверьте правила и бонусы.");
    return int(std::round(value));
}
bool safeProfileId(const std::string& id) {
    return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
    });
}
std::filesystem::path journalPath(const std::filesystem::path& root) { return root / "meta" / "qt-xp-transaction"; }
bool safeBackupName(const std::string& name) {
    if (name == "meta/tasks.json" || name == "meta/projects.json" || name == "meta/task-audit.log" ||
        name == "meta/updates/tasks.last-good.json" || name == "meta/updates/projects.last-good.json") return true;
    return name.size() > 4 && name.substr(name.size() - 4) == ".ini" && safeProfileId(name.substr(0, name.size() - 4));
}
void checkPath(const std::filesystem::path& root, const std::filesystem::path& relative);

void prepareFileJournal(const std::filesystem::path& root, const std::string& version,
                        const std::vector<std::string>& files) {
    const auto pending = journalPath(root);
    if (std::filesystem::exists(pending)) throw std::runtime_error(u8"Сначала восстановите незавершённую Qt-транзакцию перезапуском приложения.");
    const auto staging = root / "meta" / "qt-xp-staging";
    checkPath(root, "meta/qt-xp-staging");
    checkPath(root, "meta/qt-xp-finished");
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    std::ofstream manifest(staging / "manifest", std::ios::binary);
    if (!manifest) throw std::runtime_error(u8"Не удалось создать журнал восстановления Qt.");
    manifest << version << ' ' << files.size() << '\n';
    for (const auto& name : files) {
        if (!safeBackupName(name)) throw std::runtime_error(u8"Некорректный путь журнала Qt.");
        checkPath(root, name);
        const auto source = root / name;
        const bool exists = std::filesystem::exists(source);
        if (exists) {
            if (!std::filesystem::is_regular_file(source)) throw std::runtime_error(u8"Ожидался обычный файл Qt-транзакции.");
            std::filesystem::create_directories((staging / name).parent_path());
            std::filesystem::copy_file(source, staging / name);
        }
        manifest << std::quoted(name) << ' ' << exists << '\n';
    }
    manifest.flush();
    if (!manifest) throw std::runtime_error(u8"Не удалось сохранить журнал восстановления Qt.");
    manifest.close();
    std::filesystem::rename(staging, pending);
}
// Reject links in every path component, not just in the final file.
void checkPath(const std::filesystem::path& root, const std::filesystem::path& relative) {
    auto current = root;
    for (const auto& part : relative) {
        current /= part;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(current)))
            throw std::runtime_error(u8"Ссылки в файлах XP не поддерживаются.");
    }
}
void finishJournal(const std::filesystem::path& root) {
    auto destination = root / "meta" / "qt-xp-finished";
    // A prior completed transaction can safely be cleaned up; pending is never deleted first.
    std::filesystem::remove_all(destination);
    std::filesystem::rename(journalPath(root), destination);
    std::error_code ec;
    std::filesystem::remove_all(destination, ec);
}
void prepareJournal(const std::filesystem::path& root, const TaskCompletionPreview& preview, bool editing = false) {
    const auto pending = journalPath(root);
    if (std::filesystem::exists(pending)) throw std::runtime_error(u8"Сначала восстановите незавершённую XP-транзакцию перезапуском Qt.");
    const auto staging = root / "meta" / "qt-xp-staging";
    checkPath(root, "meta/qt-xp-staging");
    checkPath(root, "meta/qt-xp-finished");
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    std::vector<std::string> files = {"meta/tasks.json", "meta/task-audit.log", "meta/updates/tasks.last-good.json"};
    for (const auto& p : preview.finalize.participants) files.push_back(p.profileId + ".ini");
    std::ofstream manifest(staging / "manifest", std::ios::binary);
    if (!manifest) throw std::runtime_error(u8"Не удалось создать журнал восстановления XP.");
    manifest << (editing ? "FORGEMIRROR_QT_TASK_EDIT_1 " : "FORGEMIRROR_QT_XP_1 ") << files.size() << '\n';
    for (const auto& name : files) {
        checkPath(root, name);
        const auto source = root / name;
        const bool exists = std::filesystem::exists(source);
        if (exists) {
            if (!std::filesystem::is_regular_file(source)) throw std::runtime_error(u8"Ожидался обычный файл XP.");
            std::filesystem::create_directories((staging / name).parent_path());
            std::filesystem::copy_file(source, staging / name);
        }
        manifest << std::quoted(name) << ' ' << exists << '\n';
    }
    manifest.flush();
    if (!manifest) throw std::runtime_error(u8"Не удалось сохранить журнал восстановления XP.");
    manifest.close();
    std::filesystem::rename(staging, pending);
}
}

bool RecoverTaskCompletion(const std::filesystem::path& root) {
    const auto pending = journalPath(root);
    checkPath(root, "meta/qt-xp-transaction");
    if (!std::filesystem::exists(pending)) return false;
    checkPath(root, "meta/qt-xp-finished");
    checkPath(pending, "manifest");
    std::ifstream manifest(pending / "manifest", std::ios::binary);
    if (!manifest) throw std::runtime_error(u8"Повреждён журнал XP: требуется ручное восстановление.");
    std::vector<std::pair<std::string, bool>> entries;
    std::string name;
    bool exists = false;
    std::set<std::string> seen;
    std::string version;
    size_t count = 0;
    if (!(manifest >> version >> count) ||
        !((version == "FORGEMIRROR_QT_XP_1" && count >= 4 && count <= 10003) ||
          (version == "FORGEMIRROR_QT_TASK_EDIT_1" && count == 3) ||
          (version == "FORGEMIRROR_QT_PROJECT_DELETE_1" && count == 5)))
        throw std::runtime_error(u8"Неизвестный формат журнала XP.");
    for (size_t i = 0; i < count; ++i) {
        if (!(manifest >> std::quoted(name) >> exists)) throw std::runtime_error(u8"Неполный журнал XP.");
        const bool projectFile = name == "meta/projects.json" || name == "meta/updates/projects.last-good.json";
        if (!safeBackupName(name) || (projectFile && version != "FORGEMIRROR_QT_PROJECT_DELETE_1") || !seen.insert(name).second)
            throw std::runtime_error(u8"Некорректный путь в журнале XP.");
        checkPath(root, name);
        checkPath(pending, name);
        if (exists && !std::filesystem::is_regular_file(pending / name)) throw std::runtime_error(u8"Резервный файл XP отсутствует.");
        entries.emplace_back(name, exists);
    }
    manifest >> std::ws;
    const bool commonComplete = seen.count("meta/tasks.json") && seen.count("meta/task-audit.log") &&
        seen.count("meta/updates/tasks.last-good.json");
    const bool projectComplete = version != "FORGEMIRROR_QT_PROJECT_DELETE_1" ||
        (seen.count("meta/projects.json") && seen.count("meta/updates/projects.last-good.json"));
    if (!manifest.eof() || !commonComplete || !projectComplete)
        throw std::runtime_error(u8"Неполный журнал XP: требуется ручное восстановление.");
    manifest.close(); // Windows cannot rename the journal directory while this stream is open.
    for (const auto& entry : entries) {
        const auto target = root / entry.first;
        if (entry.second) {
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(pending / entry.first, target, std::filesystem::copy_options::overwrite_existing);
        } else if (std::filesystem::exists(target)) {
            if (!std::filesystem::is_regular_file(target)) throw std::runtime_error(u8"Нельзя восстановить XP поверх каталога.");
            std::filesystem::remove(target);
        }
    }
    finishJournal(root);
    return true;
}

void PrepareProjectDeletionRecovery(const std::filesystem::path& directory) {
    prepareFileJournal(directory, "FORGEMIRROR_QT_PROJECT_DELETE_1",
        {"meta/projects.json", "meta/updates/projects.last-good.json", "meta/tasks.json",
         "meta/updates/tasks.last-good.json", "meta/task-audit.log"});
}

void CommitQtRecoveryTransaction(const std::filesystem::path& directory) {
    if (!std::filesystem::exists(journalPath(directory)))
        throw std::runtime_error(u8"Журнал Qt-транзакции отсутствует.");
    finishJournal(directory);
}

AppMutationResult AdvanceTaskPipeline(const std::filesystem::path& directory,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const std::vector<PipelineStep>& steps, const std::string& taskId,
    const std::string& expectedStageId, const std::string& targetId, const std::string& actor) {
    AppMutationResult result;
    const auto task = std::find_if(tasks.begin(), tasks.end(), [&](const auto& t) { return t.id == taskId; });
    const auto source = std::find_if(steps.begin(), steps.end(), [&](const auto& s) { return s.id == expectedStageId; });
    const auto target = std::find_if(steps.begin(), steps.end(), [&](const auto& s) { return s.id == targetId; });
    if (task == tasks.end() || expectedStageId.empty() || task->pipelineStepId != expectedStageId ||
        source == steps.end() || target == steps.end() || targetId == expectedStageId ||
        std::count_if(steps.begin(), steps.end(), [&](const auto& s) { return s.id == expectedStageId || s.id == targetId; }) != 2 ||
        std::find(source->nextIds.begin(), source->nextIds.end(), targetId) == source->nextIds.end()) {
        result.errorMessage = u8"Переход недоступен или схема изменилась. Обновите данные; начальный этап задаётся в редакторе задачи.";
        return result;
    }
    if (task->status == 2) {
        result.errorMessage = u8"Задача завершена. Сначала откройте её через изменение статуса.";
        return result;
    }
    auto draft = *task;
    draft.pipelineStepId = targetId;
    draft.pipelineStep = target->title;
    return EditTaskDetails(directory, tasks, audit, draft, actor);
}

AppMutationResult EditTaskDetails(const std::filesystem::path& directory,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const TaskEntry& draft, const std::string& actor) {
    AppMutationResult result;
    const auto found = std::find_if(tasks.begin(), tasks.end(), [&](const auto& t) { return t.id == draft.id; });
    if (found == tasks.end()) { result.errorMessage = u8"Задача не найдена."; return result; }
    const TaskEntry original = *found;
    if (!original.participants.empty() && (draft.category != original.category ||
        draft.deadlinePenaltyPercent != original.deadlinePenaltyPercent || draft.assignees != original.assignees ||
        draft.skillIds != original.skillIds)) {
        result.errorMessage = u8"Параметры начисленного XP зафиксированы. Их нельзя менять через редактор.";
        return result;
    }
    const auto oldTasks = tasks;
    const auto oldAudit = audit;
    bool prepared = false;
    try {
        prepareJournal(directory, {}, true);
        prepared = true;
        auto apply = [&](const AppMutationResult& change) {
            if (!change.ok) throw std::runtime_error(change.errorMessage);
            result.changed = result.changed || change.changed;
        };
        if (draft.title != original.title || draft.description != original.description)
            apply(AppUpdateTaskText(directory, tasks, draft.id, draft.title, draft.description, actor, &audit));
        if (draft.priority != original.priority)
            apply(AppUpdateTaskPriority(directory, tasks, draft.id, draft.priority, actor, &audit));
        if (draft.projectId != original.projectId || draft.project != original.project)
            apply(AppUpdateTaskProject(directory, tasks, draft.id, draft.projectId, draft.project, actor, &audit));
        if (draft.pipelineStepId != original.pipelineStepId || draft.pipelineStep != original.pipelineStep)
            apply(AppUpdateTaskPipelineStep(directory, tasks, draft.id, draft.pipelineStepId, draft.pipelineStep, actor, &audit));
        if (draft.deadlineAt != original.deadlineAt)
            apply(AppUpdateTaskDeadline(directory, tasks, draft.id,
                draft.deadlineAt > 0 ? std::optional<std::int64_t>(draft.deadlineAt) : std::nullopt, actor, &audit));
        if (draft.category != original.category)
            apply(AppUpdateTaskCategory(directory, tasks, draft.id, draft.category, actor, &audit));
        if (draft.deadlinePenaltyPercent != original.deadlinePenaltyPercent)
            apply(AppUpdateTaskPenaltyPercent(directory, tasks, draft.id, draft.deadlinePenaltyPercent, actor, &audit));
        if (draft.skillIds != original.skillIds)
            apply(AppUpdateTaskSkillIds(directory, tasks, draft.id, draft.skillIds, actor, &audit));
        if (draft.assignees != original.assignees)
            apply(AppUpdateTaskAssignees(directory, tasks, draft.id, draft.assignees, actor, &audit));
        finishJournal(directory);
        result.ok = true;
        result.changedCount = result.changed ? 1 : 0;
    } catch (const std::exception& e) {
        result = {};
        result.errorMessage = e.what();
        if (prepared) {
            tasks = oldTasks;
            audit = oldAudit;
            try { RecoverTaskCompletion(directory); result.errorMessage += u8" Изменения полностью отменены."; }
            catch (const std::exception&) { result.errorMessage += u8" Откат не завершён. Перезапустите Qt для восстановления журнала."; }
        }
    }
    return result;
}

AppMutationResult CreateTaskWithRecovery(const std::filesystem::path& directory,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const TaskEntry& task, const std::string& actor) {
    AppMutationResult result;
    const auto oldTasks = tasks;
    const auto oldAudit = audit;
    bool prepared = false;
    try {
        prepareJournal(directory, {}, true);
        prepared = true;
        result = AppCreateTaskEntry(directory, tasks, task, actor, &audit);
        if (!result.ok) throw std::runtime_error(result.errorMessage.empty() ? u8"Не удалось создать задачу." : result.errorMessage);
        finishJournal(directory);
    } catch (const std::exception& error) {
        result = {};
        result.errorMessage = error.what();
        if (prepared) {
            tasks = oldTasks;
            audit = oldAudit;
            try { RecoverTaskCompletion(directory); result.errorMessage += u8" Изменения полностью отменены."; }
            catch (const std::exception&) { result.errorMessage += u8" Откат не завершён. Перезапустите Qt для восстановления журнала."; }
        }
    }
    return result;
}

AppMutationResult UpdateTaskStatusWithRecovery(const std::filesystem::path& directory,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const std::string& taskId, int newStatus, const std::string& actor) {
    AppMutationResult result;
    const auto oldTasks = tasks;
    const auto oldAudit = audit;
    bool prepared = false;
    try {
        prepareJournal(directory, {}, true);
        prepared = true;
        AppTaskWorkflowService workflow(directory, tasks, &audit);
        result = workflow.UpdateStatus(taskId, newStatus, actor);
        if (!result.ok) throw std::runtime_error(result.errorMessage.empty() ? u8"Не удалось изменить статус задачи." : result.errorMessage);
        finishJournal(directory);
    } catch (const std::exception& error) {
        result = {};
        result.errorMessage = error.what();
        if (prepared) {
            tasks = oldTasks;
            audit = oldAudit;
            try { RecoverTaskCompletion(directory); result.errorMessage += u8" Изменения полностью отменены."; }
            catch (const std::exception&) { result.errorMessage += u8" Откат не завершён. Перезапустите Qt для восстановления журнала."; }
        }
    }
    return result;
}

AppMutationResult DeleteTaskWithRecovery(const std::filesystem::path& directory,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const std::string& taskId, const std::string& actor) {
    AppMutationResult result;
    const auto matches = std::count_if(tasks.begin(), tasks.end(), [&](const auto& task) { return task.id == taskId; });
    if (taskId.empty() || matches != 1) {
        result.errorMessage = u8"Задача не найдена или её ID неоднозначен.";
        return result;
    }
    const auto selected = std::find_if(tasks.begin(), tasks.end(), [&](const auto& task) { return task.id == taskId; });
    if (!selected->participants.empty()) {
        result.errorMessage = u8"По задаче уже начислен XP. Безопасный откат профилей ещё не перенесён в Qt.";
        return result;
    }
    const auto oldTasks = tasks;
    const auto oldAudit = audit;
    bool prepared = false;
    try {
        prepareJournal(directory, {}, true);
        prepared = true;
        result = AppDeleteTasksByIds(directory, tasks, {taskId}, actor, &audit);
        if (!result.ok) throw std::runtime_error(result.errorMessage.empty() ? u8"Не удалось удалить задачу." : result.errorMessage);
        finishJournal(directory);
    } catch (const std::exception& error) {
        result = {};
        result.errorMessage = error.what();
        if (prepared) {
            tasks = oldTasks;
            audit = oldAudit;
            try { RecoverTaskCompletion(directory); result.errorMessage += u8" Изменения полностью отменены."; }
            catch (const std::exception&) { result.errorMessage += u8" Откат не завершён. Перезапустите Qt для восстановления журнала."; }
        }
    }
    return result;
}

AppMutationResult DeleteAwardedTaskWithRecovery(AppContext& app,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const std::string& taskId, const std::string& restoreProfileId, const std::string& actor) {
    AppMutationResult result;
    RestoreSelection restore{app.storage, restoreProfileId};
    const auto matches = std::count_if(tasks.begin(), tasks.end(), [&](const auto& task) { return task.id == taskId; });
    if (taskId.empty() || matches != 1) { result.errorMessage = u8"Задача не найдена или её ID неоднозначен."; return result; }
    const auto selected = std::find_if(tasks.begin(), tasks.end(), [&](const auto& task) { return task.id == taskId; });
    if (selected->participants.empty()) return DeleteTaskWithRecovery(app.storageDir, tasks, audit, taskId, actor);

    std::set<std::string> profileIds;
    std::vector<Profile> rollbackProfiles;
    rollbackProfiles.reserve(selected->participants.size());
    for (const auto& participant : selected->participants) {
        if (!safeProfileId(participant.profileId) || !profileIds.insert(participant.profileId).second ||
            !app.storage.set_active_profile(participant.profileId)) {
            result.errorMessage = u8"Профиль участника недоступен или повторяется. Удаление остановлено."; return result;
        }
        auto profile = app.storage.load_profile();
        if (!profile || !ProfileMatchesTaskRollbackPostcondition(participant.rollbackSnapshot, *profile)) {
            result.errorMessage = u8"Профиль участника изменился после этой задачи или использует legacy snapshot. Новый прогресс нельзя откатывать."; return result;
        }
        Profile before = *profile;
        if (!ApplyProfileTaskRollbackSnapshot(participant.rollbackSnapshot, before)) {
            result.errorMessage = u8"Rollback snapshot участника повреждён."; return result;
        }
        rollbackProfiles.push_back(std::move(before));
    }

    const auto oldTasks = tasks;
    const auto oldAudit = audit;
    TaskCompletionPreview journal;
    journal.finalize.participants = selected->participants;
    bool prepared = false;
    try {
        prepareJournal(app.storageDir, journal);
        prepared = true;
        for (size_t i = 0; i < rollbackProfiles.size(); ++i) {
            if (!app.storage.set_active_profile(selected->participants[i].profileId) || !app.storage.save_profile(rollbackProfiles[i]))
                throw std::runtime_error(u8"Не удалось сохранить откат профиля участника.");
        }
        result = AppDeleteTasksByIds(app.storageDir, tasks, {taskId}, actor, &audit);
        if (!result.ok) throw std::runtime_error(result.errorMessage.empty() ? u8"Не удалось удалить задачу." : result.errorMessage);
        finishJournal(app.storageDir);
    } catch (const std::exception& error) {
        result = {};
        result.errorMessage = error.what();
        if (prepared) {
            tasks = oldTasks;
            audit = oldAudit;
            try { RecoverTaskCompletion(app.storageDir); result.errorMessage += u8" Изменения полностью отменены."; }
            catch (const std::exception&) { result.errorMessage += u8" Откат не завершён. Перезапустите Qt для восстановления журнала."; }
        }
    }
    return result;
}

TaskCompletionPreview PreviewTaskCompletion(AppContext& app, const std::vector<TaskEntry>& tasks,
                                            const TaskCompletionInput& input) {
    TaskCompletionPreview result;
    RestoreSelection restore{app.storage, input.restoreProfileId};
    try {
        auto require = [](bool condition, const char* message) { if (!condition) throw std::runtime_error(message); };
        const auto task = std::find_if(tasks.begin(), tasks.end(), [&](const auto& t) { return t.id == input.taskId; });
        require(task != tasks.end(), u8"Задача не найдена.");
        require(task->participants.empty(), u8"XP по этой задаче уже начислен.");
        require(input.category >= 0 && input.category < Profile::kCategoryCount && input.score >= 1 && input.score <= 10,
                u8"Проверьте категорию и оценку 1–10.");
        require(input.now > 0, u8"Не задано время начисления.");
        require(!input.shares.empty(), u8"Выберите участников и задайте вклад в сумме 100%.");
        int total = 0;
        std::set<std::string> ids;
        const auto profiles = app.storage.list_profiles();
        std::vector<int> shares;
        for (const auto& share : input.shares) {
            require(safeProfileId(share.profileId) && ids.insert(share.profileId).second, u8"Некорректный или повторяющийся участник.");
            require(share.percent > 0 && share.percent <= 100, u8"Вклад выбранного участника должен быть от 1 до 100%.");
            const auto info = std::find_if(profiles.begin(), profiles.end(), [&](const auto& p) { return p.id == share.profileId; });
            require(info != profiles.end() && !info->archived, u8"Участник удалён или находится в архиве.");
            total += share.percent;
            require(total <= 100, u8"Сумма вкладов должна быть 100%.");
            shares.push_back(share.percent);
        }
        require(total == 100, u8"Сумма вкладов должна быть 100%.");
        ids.clear();
        int ratingTotal = 0;
        require(!input.skills.empty(), u8"Каталог навыков пуст. Добавьте навыки в стабильной версии и импортируйте копию данных.");
        result.skillPercents.resize(input.skills.size());
        for (const auto& skill : input.skills) {
            require(ids.insert(skill.skillId).second && app.catalog.contains_id(skill.skillId), u8"Неизвестный или повторяющийся навык.");
            require(skill.rating >= 0 && skill.rating <= 5, u8"Оценка навыка должна быть 0–5.");
            ratingTotal += skill.rating;
        }
        require(ratingTotal > 0, u8"Поставьте оценку хотя бы одному навыку.");
        // Same rating rounding and remainder order as GuiXpUtils.inc.
        int remainder = 100;
        std::vector<size_t> order;
        for (size_t i = 0; i < input.skills.size(); ++i) if (input.skills[i].rating) {
            result.skillPercents[i] = 100 * input.skills[i].rating / ratingTotal;
            remainder -= result.skillPercents[i];
            order.push_back(i);
        }
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return input.skills[a].rating > input.skills[b].rating; });
        for (int i = 0; i < remainder; ++i) ++result.skillPercents[order[size_t(i) % order.size()]];
        const auto& rules = GetGameplayConfig();
        const float focus = rules.focusBaseBonus + rules.focusAdditionalBonus * (*std::max_element(result.skillPercents.begin(), result.skillPercents.end()) / 100.0f);
        result.rawPool = checkedXp(rules.categoryBaseXp[input.category] * std::pow(std::max(0.1f, input.score / 10.0f), 1.35f) * focus);
        result.penaltyPercent = std::clamp(task->deadlinePenaltyPercent, 0, 100);
        auto& request = result.finalize;
        request.taskId = input.taskId;
        request.category = input.category;
        request.score = input.score;
        request.baseXp = rules.categoryBaseXp[input.category];
        request.basePool = AppTaskWorkflowService::ApplyPercentPenalty(result.rawPool, result.penaltyPercent);
        request.actor = input.actor;
        for (const auto& skill : input.skills) if (skill.rating > 0) request.skillIds.push_back(skill.skillId);
        const auto pools = AppTaskWorkflowService::DistributeIntegerPool(request.basePool, shares);
        for (size_t i = 0; i < input.shares.size(); ++i) {
            const auto& share = input.shares[i];
            require(app.storage.set_active_profile(share.profileId), u8"Не удалось открыть профиль участника.");
            auto loaded = app.storage.load_profile();
            require(bool(loaded), u8"Не удалось прочитать профиль участника.");
            Profile profile = *loaded;
            const Profile beforeTask = profile;
            require(!profile.is_blocked(), u8"Заблокированному профилю нельзя начислить XP.");
            TaskParticipant participant;
            participant.profileId = share.profileId;
            participant.percent = share.percent;
            const auto skillPools = AppTaskWorkflowService::DistributeIntegerPool(pools[i], result.skillPercents);
            for (size_t s = 0; s < input.skills.size(); ++s) if (skillPools[s] > 0) {
                const auto& id = input.skills[s].skillId;
                profile.add_skill(id, 1, app.catalog.weight(id));
                const int bonus = checkedXp(skillPools[s] * profile.skill_bonus_multiplier(id, input.now));
                const int xp = ApplyProfileSpiritXpModifier(profile.spirit(), bonus);
                participant.skillXp = checkedXp(double(participant.skillXp) + xp);
                profile.grant_xp(id, xp);
            }
            const int best = profile.category_best_score(input.category);
            const bool penalties = profile.penalties_enabled();
            int effective = pools[i];
            std::string modifiers;
            if (penalties && input.score <= best) {
                effective = checkedXp(effective * rules.repeatRewardFactor);
                modifiers += u8"Повтор; ";
            }
            if (penalties && profile.last_task_timestamp() > 0 && input.now - profile.last_task_timestamp() > 30LL * 86400)
                profile.start_penalty_recovery(rules.recoveryWarmupTasks);
            if (!penalties && profile.penalty_active()) profile.start_penalty_recovery(0);
            if (penalties && profile.penalty_active()) {
                effective = checkedXp(effective * rules.recoveryRewardFactor);
                profile.consume_penalty_task();
                modifiers += u8"Прогрев; ";
            }
            participant.globalXp = ApplyProfileSpiritXpModifier(profile.spirit(), effective);
            require(profile.total_xp() <= std::numeric_limits<int>::max() - participant.globalXp, u8"Суммарный XP профиля превышает допустимый диапазон.");
            if (profile.spirit() != ProfileSpirit::None) modifiers += ProfileSpiritLabel(profile.spirit());
            profile.set_last_task_timestamp(input.now);
            profile.increment_tasks_completed();
            if (participant.globalXp > 0) profile.grant_global_xp(participant.globalXp);
            if (input.score > best) profile.update_category_best_score(input.category, input.score);
            profile.reset_category_cooldown(input.category);
            for (int c = 0; c < Profile::kCategoryCount; ++c) if (c != input.category) {
                profile.tick_category_cooldown(c);
                if (profile.category_cooldown(c) < 0) {
                    profile.update_category_best_score(c, profile.category_best_score(c) - 1);
                    profile.reset_category_cooldown(c);
                }
            }
            const auto& cooldowns = profile.category_cooldowns();
            profile.set_inactivity_tasks(std::max(0, *std::min_element(cooldowns.begin(), cooldowns.end())));
            participant.rollbackSnapshot = SerializeProfileTaskRollbackEnvelope(beforeTask, profile);
            request.assignees.push_back(share.profileId);
            request.participants.push_back(participant);
            result.participantNames.push_back(profile.name());
            result.modifiers.push_back(modifiers);
            result.updatedProfiles.push_back(std::move(profile));
        }
        const auto validation = AppTaskWorkflowService::ValidateFinalizeXp(tasks, request);
        require(validation.ok, validation.errorMessage.c_str());
        result.ok = true;
    } catch (const std::exception& e) { result.errorMessage = e.what(); }
    return result;
}

AppMutationResult CompleteTaskWithXp(AppContext& app, std::vector<TaskEntry>& tasks,
                                     std::vector<TaskAuditEntry>& audit, const TaskCompletionInput& input) {
    AppMutationResult result;
    RestoreSelection restore{app.storage, input.restoreProfileId};
    auto preview = PreviewTaskCompletion(app, tasks, input);
    if (!preview.ok) { result.errorMessage = preview.errorMessage; return result; }
    const auto oldTasks = tasks;
    const auto oldAudit = audit;
    bool prepared = false;
    try {
        prepareJournal(app.storageDir, preview);
        prepared = true;
        for (size_t i = 0; i < preview.updatedProfiles.size(); ++i) {
            if (!app.storage.set_active_profile(preview.finalize.participants[i].profileId) || !app.storage.save_profile(preview.updatedProfiles[i]))
                throw std::runtime_error(u8"Не удалось сохранить профиль участника.");
        }
        AppTaskWorkflowService workflow(app.storageDir, tasks, &audit);
        result = workflow.FinalizeXp(preview.finalize);
        if (!result.ok) throw std::runtime_error(result.errorMessage);
        finishJournal(app.storageDir);
    } catch (const std::exception& e) {
        result = {};
        result.errorMessage = e.what();
        if (prepared) {
            tasks = oldTasks;
            audit = oldAudit;
            try {
                RecoverTaskCompletion(app.storageDir);
                result.errorMessage += u8" Начисление полностью отменено.";
            } catch (const std::exception&) {
                result.errorMessage += u8" Откат не завершён. Закройте Qt; журнал meta/qt-xp-transaction сохранён для восстановления при запуске.";
            }
        }
    }
    return result;
}
