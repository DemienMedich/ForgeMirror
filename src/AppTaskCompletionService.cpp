#include "AppTaskCompletionService.h"
#include "AppTaskWorkflowService.h"
#include "AppUtils.h"
#include "AppWorkspaceStorageLock.h"
#include "GameplayConfig.h"
#include "IJobStorage.h"
#include "SkillCatalog.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>

namespace {
void emitCoreEvent(AppContext& app, AppLogLevel level, const std::string& message) noexcept {
    if (!app.eventLogger) return;
    try { app.eventLogger(level, message); }
    catch (...) { /* Observability must never change a committed domain result. */ }
}

void emitOptionalEvent(const std::function<void(AppLogLevel, const std::string&)>& logger,
                       AppLogLevel level, const char* message) noexcept {
    if (!logger) return;
    try { logger(level, message); }
    catch (...) { /* Observability must never change a committed domain result. */ }
}

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
thread_local std::optional<AppWorkspaceStorageWriteLock> activeTransactionWriteLock;
void retainTransactionWriteLock(const std::filesystem::path& root) {
    if (activeTransactionWriteLock) throw std::runtime_error(u8"В этом процессе уже выполняется другая операция записи.");
    activeTransactionWriteLock.emplace(root);
    if (!activeTransactionWriteLock->acquired()) {
        activeTransactionWriteLock.reset();
        throw std::runtime_error(u8"Рабочее место сейчас изменяет другая программа. Повторите операцию позже.");
    }
}
void releaseTransactionWriteLock() {
    activeTransactionWriteLock.reset();
}
void emitTransactionOutcome(AppContext& app, bool succeeded,
                            const std::string& successMessage, const std::string& failureMessage) noexcept {
    if (succeeded) {
        emitCoreEvent(app, AppLogLevel::Info, successMessage);
        return;
    }
    std::error_code ec;
    const bool recoveryPending = std::filesystem::exists(journalPath(app.storageDir), ec) && !ec;
    emitCoreEvent(app, recoveryPending ? AppLogLevel::Error : AppLogLevel::Warning, failureMessage);
}
bool safeBackupName(const std::string& name) {
    if (name == "meta/tasks.json" || name == "meta/projects.json" || name == "meta/task-audit.log" ||
        name == "meta/updates/tasks.last-good.json" || name == "meta/updates/projects.last-good.json" ||
        name == "meta/professions.txt" || name == "meta/profile-audit.log" ||
        name == "meta/storage.json" || name == "meta/ui.ini" || name == "skills.txt") return true;
    if (name.size() > 4 && name.substr(name.size() - 4) == ".ini" && safeProfileId(name.substr(0, name.size() - 4))) return true;
    if (name.rfind("archive/", 0) == 0 && name.size() > 12 && name.substr(name.size() - 4) == ".ini")
        return safeProfileId(name.substr(8, name.size() - 12));
    if (name.rfind("achievements/", 0) == 0 && name.size() > 18 && name.substr(name.size() - 5) == ".json")
        return safeProfileId(name.substr(13, name.size() - 18));
    return false;
}
void checkPath(const std::filesystem::path& root, const std::filesystem::path& relative);

void prepareFileJournal(const std::filesystem::path& root, const std::string& version,
                        const std::vector<std::string>& files) {
    AppWorkspaceStorageWriteLock writeLock(root);
    if (!writeLock.acquired()) throw std::runtime_error(u8"Рабочее место сейчас изменяет другая программа. Повторите операцию позже.");
    if (activeTransactionWriteLock) throw std::runtime_error(u8"В этом процессе уже выполняется другая операция записи.");
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
    retainTransactionWriteLock(root);
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
bool filesEqual(const std::filesystem::path& first, const std::filesystem::path& second) {
    std::error_code ec;
    const auto firstSize = std::filesystem::file_size(first, ec);
    if (ec) return false;
    const auto secondSize = std::filesystem::file_size(second, ec);
    if (ec || firstSize != secondSize) return false;
    std::ifstream a(first, std::ios::binary), b(second, std::ios::binary);
    if (!a || !b) return false;
    return std::equal(std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>(),
                      std::istreambuf_iterator<char>(b));
}
std::filesystem::path createInterruptedFilesDirectory(const std::filesystem::path& root) {
    checkPath(root, "meta/updates");
    const auto parent = root / "meta/updates";
    std::filesystem::create_directories(parent);
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for (int attempt = 0; attempt < 1000; ++attempt) {
        const auto candidate = parent / ("qt-xp-recovery-" + std::to_string(stamp) + "-" + std::to_string(attempt));
        std::error_code ec;
        if (std::filesystem::create_directory(candidate, ec)) return candidate;
        if (ec && ec != std::errc::file_exists)
            throw std::runtime_error(u8"Не удалось создать папку сохранения прерванных файлов Qt.");
    }
    throw std::runtime_error(u8"Не удалось подобрать имя папки сохранения прерванных файлов Qt.");
}
void finishJournal(const std::filesystem::path& root) {
    auto destination = root / "meta" / "qt-xp-finished";
    // A prior completed transaction can safely be cleaned up; pending is never deleted first.
    std::filesystem::remove_all(destination);
    std::filesystem::rename(journalPath(root), destination);
    std::error_code ec;
    std::filesystem::remove_all(destination, ec);
    releaseTransactionWriteLock();
}
void prepareJournal(const std::filesystem::path& root, const TaskCompletionPreview& preview, bool editing = false) {
    AppWorkspaceStorageWriteLock writeLock(root);
    if (!writeLock.acquired()) throw std::runtime_error(u8"Рабочее место сейчас изменяет другая программа. Повторите операцию позже.");
    if (activeTransactionWriteLock) throw std::runtime_error(u8"В этом процессе уже выполняется другая операция записи.");
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
    retainTransactionWriteLock(root);
}
}

bool RecoverTaskCompletion(const std::filesystem::path& root,
                           std::filesystem::path* preservedInterruptedFiles) {
    const auto pending = journalPath(root);
    checkPath(root, "meta/qt-xp-transaction");
    if (!std::filesystem::exists(pending)) return false;
    AppWorkspaceStorageWriteLock recoveryWriteLock(root);
    if (!recoveryWriteLock.acquired())
        throw std::runtime_error(u8"Рабочее место сейчас изменяет другая программа. Повторите восстановление позже.");
    if (preservedInterruptedFiles) preservedInterruptedFiles->clear();
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
          (version == "FORGEMIRROR_QT_PROJECT_DELETE_1" && count == 5) ||
          (version == "FORGEMIRROR_QT_PROFESSION_DELETE_1" && count >= 2 && count <= 20002) ||
          (version == "FORGEMIRROR_QT_SKILL_DELETE_1" && count == 1) ||
          (version == "FORGEMIRROR_QT_SKILL_MERGE_1" && count >= 4 && count <= 30004) ||
          (version == "FORGEMIRROR_QT_PROFILE_WALLET_1" && (count == 2 || count == 3)) ||
          (version == "FORGEMIRROR_QT_PROFILE_AUDIT_1" && (count == 2 || count == 3)) ||
          (version == "FORGEMIRROR_QT_PROFILE_SESSION_1" && (count == 1 || count == 2)) ||
          (version == "FORGEMIRROR_QT_PROFILE_DELETE_1" && count == 3) ||
          (version == "FORGEMIRROR_QT_DIRECT_XP_1" && (count == 2 || count == 3)) ||
          (version == "FORGEMIRROR_QT_RULES_REAPPLY_1" && count >= 1 && count <= 20000)))
        throw std::runtime_error(u8"Неизвестный формат журнала XP.");
    for (size_t i = 0; i < count; ++i) {
        if (!(manifest >> std::quoted(name) >> exists)) throw std::runtime_error(u8"Неполный журнал XP.");
        const bool projectFile = name == "meta/projects.json" || name == "meta/updates/projects.last-good.json";
        const bool professionFile = name == "meta/professions.txt" || name == "skills.txt";
        const bool profileFile = name.size() > 4 && name.substr(name.size() - 4) == ".ini";
        const bool rootProfileFile = profileFile && name.find('/') == std::string::npos;
        const bool archivedProfileFile = name.rfind("archive/", 0) == 0 && name.size() > 12 &&
            name.substr(name.size() - 4) == ".ini" && safeProfileId(name.substr(8, name.size() - 12));
        const bool profileDeleteFile = profileFile || name.rfind("achievements/", 0) == 0;
        const bool skillMergeFile = name == "skills.txt" || name == "meta/tasks.json" ||
            name == "meta/task-audit.log" || name == "meta/updates/tasks.last-good.json" ||
            profileFile || name.rfind("achievements/", 0) == 0;
        const bool walletFile = rootProfileFile || name == "meta/profile-audit.log" || name == "meta/storage.json";
        const bool walletMetadataFile = name == "meta/profile-audit.log" || name == "meta/storage.json";
        if (!safeBackupName(name) || (projectFile && version != "FORGEMIRROR_QT_PROJECT_DELETE_1") ||
            (professionFile && version != "FORGEMIRROR_QT_PROFESSION_DELETE_1" &&
             !(name == "skills.txt" && (version == "FORGEMIRROR_QT_SKILL_DELETE_1" || skillMergeFile))) ||
            (version == "FORGEMIRROR_QT_PROFESSION_DELETE_1" && !professionFile && !profileFile) ||
            (version == "FORGEMIRROR_QT_SKILL_MERGE_1" && !skillMergeFile) ||
            (version == "FORGEMIRROR_QT_PROFILE_WALLET_1" && !walletFile) ||
            (walletMetadataFile && version != "FORGEMIRROR_QT_PROFILE_WALLET_1" &&
             !(name == "meta/profile-audit.log" && (version == "FORGEMIRROR_QT_DIRECT_XP_1" ||
                                                       version == "FORGEMIRROR_QT_PROFILE_AUDIT_1" ||
                                                       version == "FORGEMIRROR_QT_PROFILE_SESSION_1"))) ||
            (name == "meta/ui.ini" && version != "FORGEMIRROR_QT_PROFILE_SESSION_1") ||
            (version == "FORGEMIRROR_QT_PROFILE_SESSION_1" && name != "meta/profile-audit.log" && name != "meta/ui.ini") ||
            (version == "FORGEMIRROR_QT_PROFILE_AUDIT_1" && name != "meta/profile-audit.log" &&
             !rootProfileFile && !archivedProfileFile) ||
            (version == "FORGEMIRROR_QT_PROFILE_DELETE_1" && !profileDeleteFile) || !seen.insert(name).second)
            throw std::runtime_error(u8"Некорректный путь в журнале XP.");
        if (version == "FORGEMIRROR_QT_RULES_REAPPLY_1" && !profileFile)
            throw std::runtime_error(u8"Журнал пересчёта правил содержит посторонний файл.");
        if (version == "FORGEMIRROR_QT_DIRECT_XP_1" && !profileDeleteFile && name != "meta/profile-audit.log")
            throw std::runtime_error(u8"Журнал ручного XP содержит посторонний файл.");
        checkPath(root, name);
        checkPath(pending, name);
        if (exists && !std::filesystem::is_regular_file(pending / name)) throw std::runtime_error(u8"Резервный файл XP отсутствует.");
        entries.emplace_back(name, exists);
    }
    manifest >> std::ws;
    const bool professionTransaction = version == "FORGEMIRROR_QT_PROFESSION_DELETE_1";
    const bool skillTransaction = version == "FORGEMIRROR_QT_SKILL_DELETE_1";
    const bool skillMergeTransaction = version == "FORGEMIRROR_QT_SKILL_MERGE_1";
    const bool profileWalletTransaction = version == "FORGEMIRROR_QT_PROFILE_WALLET_1";
    const bool profileAuditTransaction = version == "FORGEMIRROR_QT_PROFILE_AUDIT_1";
    const bool profileSessionTransaction = version == "FORGEMIRROR_QT_PROFILE_SESSION_1";
    const bool profileDeleteTransaction = version == "FORGEMIRROR_QT_PROFILE_DELETE_1";
    const bool rulesReapplyTransaction = version == "FORGEMIRROR_QT_RULES_REAPPLY_1";
    const bool directXpTransaction = version == "FORGEMIRROR_QT_DIRECT_XP_1";
    bool profileDeleteComplete = false;
    if (profileDeleteTransaction) {
        for (const auto& item : seen) if (item.size() > 4 && item.substr(item.size() - 4) == ".ini" && item.rfind("archive/", 0) != 0) {
            const auto id = item.substr(0, item.size() - 4);
            profileDeleteComplete = seen.count("archive/" + id + ".ini") && seen.count("achievements/" + id + ".json");
        }
    }
    bool directXpComplete = false;
    if (directXpTransaction) for (const auto& item : seen) if (item.size() > 4 && item.substr(item.size() - 4) == ".ini") {
        const auto id = item.substr(0, item.size() - 4);
        directXpComplete = item.rfind("archive/", 0) != 0 && seen.count("achievements/" + id + ".json") &&
            (seen.size() == 2 || (seen.size() == 3 && seen.count("meta/profile-audit.log")));
    }
    bool skillMergeComplete = false;
    if (skillMergeTransaction) {
        skillMergeComplete = seen.count("skills.txt") && seen.count("meta/tasks.json") &&
            seen.count("meta/task-audit.log") && seen.count("meta/updates/tasks.last-good.json");
        for (const auto& item : seen) {
            if (item.rfind("archive/", 0) == 0 || item.rfind("achievements/", 0) == 0) continue;
            if (item.size() > 4 && item.substr(item.size() - 4) == ".ini") {
                const auto id = item.substr(0, item.size() - 4);
                skillMergeComplete = skillMergeComplete && seen.count("archive/" + id + ".ini") &&
                    seen.count("achievements/" + id + ".json");
            }
        }
    }
    size_t walletProfileFiles = 0;
    if (profileWalletTransaction || profileAuditTransaction) for (const auto& item : seen)
        if (item.size() > 4 && item.substr(item.size() - 4) == ".ini" && item.find('/') == std::string::npos) ++walletProfileFiles;
    const bool profileWalletComplete = profileWalletTransaction && walletProfileFiles == 1 &&
        seen.count("meta/profile-audit.log") &&
        (seen.size() == 2 || (seen.size() == 3 && seen.count("meta/storage.json")));
    std::string auditProfileId, archivedAuditProfileId;
    size_t archivedAuditFiles = 0;
    if (profileAuditTransaction) for (const auto& item : seen) {
        if (item.size() > 4 && item.substr(item.size() - 4) == ".ini") {
            if (item.rfind("archive/", 0) == 0) {
                archivedAuditProfileId = item.substr(8, item.size() - 12);
                ++archivedAuditFiles;
            } else if (item.find('/') == std::string::npos) {
                auditProfileId = item.substr(0, item.size() - 4);
            }
        }
    }
    const bool profileAuditComplete = profileAuditTransaction && walletProfileFiles == 1 &&
        seen.count("meta/profile-audit.log") &&
        ((seen.size() == 2 && archivedAuditFiles == 0) ||
         (seen.size() == 3 && archivedAuditFiles == 1 && auditProfileId == archivedAuditProfileId));
    const bool profileSessionComplete = profileSessionTransaction && seen.count("meta/profile-audit.log") &&
        (seen.size() == 1 || (seen.size() == 2 && seen.count("meta/ui.ini")));
    const bool commonComplete = directXpTransaction ? directXpComplete : profileSessionTransaction ? profileSessionComplete : profileAuditTransaction ? profileAuditComplete : rulesReapplyTransaction ? !seen.empty() : profileDeleteTransaction ? profileDeleteComplete : profileWalletTransaction ? profileWalletComplete : skillMergeTransaction ? skillMergeComplete : skillTransaction ? seen.count("skills.txt") : professionTransaction
        ? seen.count("meta/professions.txt") && seen.count("skills.txt")
        : seen.count("meta/tasks.json") && seen.count("meta/task-audit.log") && seen.count("meta/updates/tasks.last-good.json");
    const bool projectComplete = version != "FORGEMIRROR_QT_PROJECT_DELETE_1" ||
        (seen.count("meta/projects.json") && seen.count("meta/updates/projects.last-good.json"));
    if (!manifest.eof() || !commonComplete || !projectComplete)
        throw std::runtime_error(u8"Неполный журнал XP: требуется ручное восстановление.");
    manifest.close(); // Windows cannot rename the journal directory while this stream is open.
    std::vector<std::string> changedPaths;
    if (preservedInterruptedFiles) {
        for (const auto& entry : entries) {
            const auto target = root / entry.first;
            const auto before = pending / entry.first;
            const bool currentExists = std::filesystem::exists(target);
            if (currentExists && !std::filesystem::is_regular_file(target))
                throw std::runtime_error(u8"Нельзя сохранить или восстановить прерванную Qt-транзакцию поверх каталога.");
            if (currentExists != entry.second ||
                (currentExists && entry.second && !filesEqual(target, before)))
                changedPaths.push_back(entry.first);
        }
    }
    std::filesystem::path preservedDirectory;
    if (!changedPaths.empty()) {
        preservedDirectory = createInterruptedFilesDirectory(root);
        std::ofstream preservedManifest(preservedDirectory / "manifest.txt", std::ios::binary | std::ios::trunc);
        if (!preservedManifest) throw std::runtime_error(u8"Не удалось записать список сохранённых прерванных файлов.");
        preservedManifest << "Interrupted Qt transaction files; copies are captured before rollback.\n";
        for (const auto& name : changedPaths) {
            const auto source = root / name;
            preservedManifest << std::quoted(name) << ' ' << (std::filesystem::exists(source) ? "saved" : "missing") << '\n';
            if (!std::filesystem::exists(source)) continue;
            const auto destination = preservedDirectory / name;
            std::filesystem::create_directories(destination.parent_path());
            checkPath(preservedDirectory, name);
            if (!std::filesystem::copy_file(source, destination))
                throw std::runtime_error(u8"Не удалось сохранить изменённый файл перед откатом Qt.");
        }
        preservedManifest.flush();
        if (!preservedManifest) throw std::runtime_error(u8"Не удалось завершить список сохранённых прерванных файлов.");
        preservedManifest.close();
    }
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
    if (preservedInterruptedFiles) *preservedInterruptedFiles = preservedDirectory;
    return true;
}

std::string RecoverTaskCompletionWithNotice(const std::filesystem::path& root,
                                            const std::string& rollbackMessage) {
    std::filesystem::path preservedFiles;
    RecoverTaskCompletion(root, &preservedFiles);
    std::string notice = " " + rollbackMessage;
    if (!preservedFiles.empty()) {
        preservedFiles.make_preferred();
        notice += std::string(u8" Изменённые файлы перед откатом сохранены: ") + preservedFiles.u8string() + ".";
    }
    return notice;
}

void PrepareProjectDeletionRecovery(const std::filesystem::path& directory) {
    prepareFileJournal(directory, "FORGEMIRROR_QT_PROJECT_DELETE_1",
        {"meta/projects.json", "meta/updates/projects.last-good.json", "meta/tasks.json",
         "meta/updates/tasks.last-good.json", "meta/task-audit.log"});
}

void PrepareProfessionDeletionRecovery(const std::filesystem::path& directory,
                                       const std::vector<std::string>& profileIds) {
    if (profileIds.size() > 10000) throw std::runtime_error(u8"Слишком много профилей для безопасного удаления профессии.");
    std::vector<std::string> files = {"meta/professions.txt", "skills.txt"};
    std::set<std::string> uniqueIds;
    for (const auto& id : profileIds) {
        const bool archived = id.rfind("archive/", 0) == 0;
        const auto profileId = archived ? id.substr(8) : id;
        if (!safeProfileId(profileId) || !uniqueIds.insert(profileId).second)
            throw std::runtime_error(u8"Некорректный или повторяющийся профиль для журнала профессии.");
        files.push_back(profileId + ".ini");
        if (archived) files.push_back(id + ".ini");
    }
    prepareFileJournal(directory, "FORGEMIRROR_QT_PROFESSION_DELETE_1", files);
}

void PrepareSkillDeletionRecovery(const std::filesystem::path& directory) {
    prepareFileJournal(directory, "FORGEMIRROR_QT_SKILL_DELETE_1", {"skills.txt"});
}

void PrepareSkillMergeRecovery(const std::filesystem::path& directory,
                               const std::vector<std::string>& profileIds) {
    std::vector<std::string> files = {"skills.txt", "meta/tasks.json", "meta/task-audit.log",
                                      "meta/updates/tasks.last-good.json"};
    std::set<std::string> uniqueIds;
    for (const auto& id : profileIds) {
        if (!safeProfileId(id) || !uniqueIds.insert(id).second)
            throw std::runtime_error(u8"Некорректный или повторяющийся профиль для слияния навыков.");
        files.push_back(id + ".ini");
        files.push_back("archive/" + id + ".ini");
        files.push_back("achievements/" + id + ".json");
    }
    prepareFileJournal(directory, "FORGEMIRROR_QT_SKILL_MERGE_1", files);
}

void PrepareProfileDeletionRecovery(const std::filesystem::path& directory, const std::string& profileId) {
    if (!safeProfileId(profileId)) throw std::runtime_error(u8"Некорректный ID профиля для журнала удаления.");
    prepareFileJournal(directory, "FORGEMIRROR_QT_PROFILE_DELETE_1",
        {profileId + ".ini", "archive/" + profileId + ".ini", "achievements/" + profileId + ".json"});
}

void PrepareProfileWalletRecovery(const std::filesystem::path& directory,
                                  const std::string& profileId, bool includeStorageVault) {
    if (!safeProfileId(profileId)) throw std::runtime_error(u8"Некорректный ID профиля для журнала кошелька.");
    std::vector<std::string> files = {profileId + ".ini", "meta/profile-audit.log"};
    if (includeStorageVault) files.push_back("meta/storage.json");
    prepareFileJournal(directory, "FORGEMIRROR_QT_PROFILE_WALLET_1", files);
}

void PrepareProfileAuditRecovery(const std::filesystem::path& directory, const std::string& profileId) {
    if (!safeProfileId(profileId)) throw std::runtime_error(u8"Некорректный ID профиля для журнала аудита.");
    prepareFileJournal(directory, "FORGEMIRROR_QT_PROFILE_AUDIT_1",
        {profileId + ".ini", "meta/profile-audit.log"});
}

void PrepareProfileArchiveAuditRecovery(const std::filesystem::path& directory, const std::string& profileId) {
    if (!safeProfileId(profileId)) throw std::runtime_error(u8"Некорректный ID профиля для журнала архива.");
    prepareFileJournal(directory, "FORGEMIRROR_QT_PROFILE_AUDIT_1",
        {profileId + ".ini", "archive/" + profileId + ".ini", "meta/profile-audit.log"});
}

void PrepareProfileSessionAuditRecovery(const std::filesystem::path& directory, bool includeUiSettings) {
    std::vector<std::string> files = {"meta/profile-audit.log"};
    if (includeUiSettings) files.push_back("meta/ui.ini");
    prepareFileJournal(directory, "FORGEMIRROR_QT_PROFILE_SESSION_1", files);
}

void PrepareRulesReapplyRecovery(const std::filesystem::path& directory,
                                 const std::vector<std::pair<std::string, bool>>& profiles) {
    std::vector<std::string> files;
    std::set<std::string> uniqueIds;
    files.reserve(profiles.size());
    for (const auto& [id, archived] : profiles) {
        if (!safeProfileId(id) || !uniqueIds.insert(id).second)
            throw std::runtime_error(u8"Некорректный или повторяющийся профиль для пересчёта правил.");
        files.push_back(id + ".ini");
        if (archived) files.push_back("archive/" + id + ".ini");
    }
    if (files.empty()) throw std::runtime_error(u8"Нет профилей для пересчёта правил.");
    prepareFileJournal(directory, "FORGEMIRROR_QT_RULES_REAPPLY_1", files);
}

void PrepareDirectXpRecovery(const std::filesystem::path& directory, const std::string& profileId) {
    if (!safeProfileId(profileId)) throw std::runtime_error(u8"Некорректный ID профиля для ручного XP.");
    prepareFileJournal(directory, "FORGEMIRROR_QT_DIRECT_XP_1",
        {profileId + ".ini", "achievements/" + profileId + ".json", "meta/profile-audit.log"});
}

void CommitQtRecoveryTransaction(const std::filesystem::path& directory) {
    if (!std::filesystem::exists(journalPath(directory)))
        throw std::runtime_error(u8"Журнал Qt-транзакции отсутствует.");
    finishJournal(directory);
}

AppProfileMutationResult ReapplyRulesWithRecovery(AppContext& app,
                                                  const std::string& restoreProfileId) {
    const auto profiles = app.storage.list_profiles();
    if (profiles.empty()) return AppReapplyRulesToProfiles(app, restoreProfileId);
    std::vector<std::pair<std::string, bool>> journalProfiles;
    journalProfiles.reserve(profiles.size());
    for (const auto& profile : profiles) journalProfiles.emplace_back(profile.id, profile.archived);
    AppProfileMutationResult result;
    try {
        PrepareRulesReapplyRecovery(app.storageDir, journalProfiles);
        result = AppReapplyRulesToProfiles(app, restoreProfileId);
        if (!result.ok) throw std::runtime_error(result.errorMessage.empty() ? u8"Не удалось пересчитать профили." : result.errorMessage);
        CommitQtRecoveryTransaction(app.storageDir);
    } catch (const std::exception& error) {
        result.ok = false;
        result.changed = false;
        result.affectedProfiles = 0;
        result.errorMessage = error.what();
        try {
            if (std::filesystem::exists(journalPath(app.storageDir))) {
                result.errorMessage += RecoverTaskCompletionWithNotice(app.storageDir);
            }
        } catch (const std::exception&) {
            result.errorMessage += u8" Восстановление не завершено; журнал сохранён до перезапуска Qt.";
        }
        if (!restoreProfileId.empty()) app.storage.set_active_profile(restoreProfileId);
    }
    emitTransactionOutcome(app, result.ok, "Rules reapply transaction committed",
                           "Rules reapply failed or was rolled back");
    return result;
}

AppProfileMutationResult GrantDirectSkillXpWithRecovery(AppContext& app,
    const std::string& restoreProfileId, const std::string& profileId,
    const std::string& skillId, int amount, std::int64_t nowSec) {
    AppProfileMutationResult result;
    try {
        PrepareDirectXpRecovery(app.storageDir, profileId);
        result = AppGrantDirectSkillXp(app.storage, app.catalog, restoreProfileId, profileId, skillId, amount, nowSec);
        if (!result.ok) throw std::runtime_error(result.errorMessage.empty() ? u8"Не удалось начислить XP." : result.errorMessage);
        if (!AppendProfileAudit(app.storageDir, profileId, "direct_xp",
                skillId + " base=" + std::to_string(result.awardedGlobalXp) +
                "|skill=" + std::to_string(result.awardedSkillXp)))
            throw std::runtime_error(u8"Не удалось записать аудит начисления XP; изменение отменено.");
        CommitQtRecoveryTransaction(app.storageDir);
    } catch (const std::exception& error) {
        result.ok = false; result.changed = false; result.affectedProfiles = 0;
        result.awardedGlobalXp = 0; result.awardedSkillXp = 0; result.errorMessage = error.what();
        try {
            if (std::filesystem::exists(journalPath(app.storageDir))) {
                result.errorMessage += RecoverTaskCompletionWithNotice(app.storageDir);
            }
        } catch (const std::exception&) {
            result.errorMessage += u8" Восстановление не завершено; журнал сохранён до перезапуска Qt.";
        }
        if (!restoreProfileId.empty()) app.storage.set_active_profile(restoreProfileId);
    }
    emitTransactionOutcome(app, result.ok, "Direct skill XP transaction committed",
                           "Direct skill XP failed or was rolled back");
    return result;
}

AppProfileMutationResult SaveProfileSnapshotWithAuditRecovery(AppContext& app,
    const std::string& restoreProfileId, const std::string& profileId,
    const Profile& profile, const std::string& action, const std::string& details) {
    AppProfileMutationResult result;
    try {
        PrepareProfileAuditRecovery(app.storageDir, profileId);
        result = AppSaveProfileSnapshot(app.storage, restoreProfileId, profileId, profile);
        if (!result.ok) throw std::runtime_error(result.errorMessage.empty() ? u8"Не удалось сохранить профиль." : result.errorMessage);
        if (result.changed && !AppendProfileAudit(app.storageDir, profileId, action, details))
            throw std::runtime_error(u8"Не удалось записать аудит профиля; изменение отменено.");
        CommitQtRecoveryTransaction(app.storageDir);
    } catch (const std::exception& error) {
        result.ok = false; result.changed = false; result.affectedProfiles = 0;
        result.profile.reset(); result.errorMessage = error.what();
        try {
            if (std::filesystem::exists(journalPath(app.storageDir))) {
                result.errorMessage += RecoverTaskCompletionWithNotice(app.storageDir);
            }
        } catch (const std::exception&) {
            result.errorMessage += u8" Восстановление не завершено; журнал сохранён до перезапуска Qt.";
        }
        if (!restoreProfileId.empty()) app.storage.set_active_profile(restoreProfileId);
    }
    const bool passwordOperation = action == "password_change" || action == "password_reset";
    emitTransactionOutcome(app, result.ok,
        passwordOperation ? "Profile password transaction committed" : "Profile update transaction committed",
        passwordOperation ? "Profile password transaction failed or was rolled back" : "Profile update failed or was rolled back");
    return result;
}

AppProfileMutationResult ChangeProfilePasswordWithAuditRecovery(AppContext& app,
    const std::string& restoreProfileId, const std::string& profileId,
    const std::string& currentPassword, const std::string& newPassword,
    bool requireCurrentPassword, const std::string& action) {
    AppProfileMutationResult result;
    try {
        PrepareProfileAuditRecovery(app.storageDir, profileId);
        result = AppChangeProfilePassword(app.storage, restoreProfileId, profileId,
            currentPassword, newPassword, requireCurrentPassword);
        if (!result.ok) throw std::runtime_error(result.errorMessage.empty() ? u8"Не удалось изменить пароль." : result.errorMessage);
        if (result.changed && !AppendProfileAudit(app.storageDir, profileId, action))
            throw std::runtime_error(u8"Не удалось записать аудит пароля; изменение отменено.");
        CommitQtRecoveryTransaction(app.storageDir);
    } catch (const std::exception& error) {
        result.ok = false; result.changed = false; result.affectedProfiles = 0;
        result.profile.reset(); result.errorMessage = error.what();
        try {
            if (std::filesystem::exists(journalPath(app.storageDir))) {
                result.errorMessage += RecoverTaskCompletionWithNotice(app.storageDir);
            }
        } catch (const std::exception&) {
            result.errorMessage += u8" Восстановление не завершено; журнал сохранён до перезапуска Qt.";
        }
        if (!restoreProfileId.empty()) app.storage.set_active_profile(restoreProfileId);
    }
    emitTransactionOutcome(app, result.ok, "Profile password transaction committed",
                           "Profile password transaction failed or was rolled back");
    return result;
}

AppProfileActionResult ArchiveProfileWithAuditRecovery(IJobStorage& storage,
    const std::filesystem::path& directory, const std::string& restoreProfileId,
    const std::string& profileId, bool archived,
    std::function<void(AppLogLevel, const std::string&)> eventLogger) {
    AppProfileActionResult result;
    try {
        PrepareProfileArchiveAuditRecovery(directory, profileId);
        result = AppSetProfileArchived(storage, profileId, archived);
        if (!result.ok) throw std::runtime_error(result.errorMessage.empty() ? u8"Не удалось изменить состояние архива." : result.errorMessage);
        if (result.changed && !AppendProfileAudit(directory, profileId, archived ? "archive" : "restore"))
            throw std::runtime_error(u8"Не удалось записать аудит архива; изменение отменено.");
        CommitQtRecoveryTransaction(directory);
    } catch (const std::exception& error) {
        result.ok = false; result.changed = false; result.errorMessage = error.what();
        try {
            if (std::filesystem::exists(journalPath(directory))) {
                result.errorMessage += RecoverTaskCompletionWithNotice(directory);
            }
        } catch (const std::exception&) {
            result.errorMessage += u8" Восстановление не завершено; журнал сохранён для восстановления при запуске.";
        }
        if (!restoreProfileId.empty()) storage.set_active_profile(restoreProfileId);
    }
    if (eventLogger) {
        try {
            eventLogger(result.ok ? AppLogLevel::Info : AppLogLevel::Warning,
                result.ok
                    ? (archived ? "Profile archive transaction committed" : "Profile restore transaction committed")
                    : (archived ? "Profile archive transaction failed or was rolled back" : "Profile restore transaction failed or was rolled back"));
        } catch (...) {}
    }
    return result;
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
    const TaskEntry& draft, const std::string& actor,
    std::function<void(AppLogLevel, const std::string&)> eventLogger) {
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
            try { result.errorMessage += RecoverTaskCompletionWithNotice(directory); }
            catch (const std::exception&) { result.errorMessage += u8" Откат не завершён. Перезапустите Qt для восстановления журнала."; }
        }
    }
    emitOptionalEvent(eventLogger, result.ok ? AppLogLevel::Info : AppLogLevel::Warning,
        result.ok ? "Task edit committed" : "Task edit failed or rolled back");
    return result;
}

AppMutationResult CreateTaskWithRecovery(const std::filesystem::path& directory,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const TaskEntry& task, const std::string& actor,
    std::function<void(AppLogLevel, const std::string&)> eventLogger) {
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
            try { result.errorMessage += RecoverTaskCompletionWithNotice(directory); }
            catch (const std::exception&) { result.errorMessage += u8" Откат не завершён. Перезапустите Qt для восстановления журнала."; }
        }
    }
    emitOptionalEvent(eventLogger, result.ok ? AppLogLevel::Info : AppLogLevel::Warning,
        result.ok ? "Task creation committed" : "Task creation failed or rolled back");
    return result;
}

AppMutationResult UpdateTaskStatusWithRecovery(const std::filesystem::path& directory,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const std::string& taskId, int newStatus, const std::string& actor,
    std::function<void(AppLogLevel, const std::string&)> eventLogger) {
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
            try { result.errorMessage += RecoverTaskCompletionWithNotice(directory); }
            catch (const std::exception&) { result.errorMessage += u8" Откат не завершён. Перезапустите Qt для восстановления журнала."; }
        }
    }
    if (eventLogger) {
        try {
            eventLogger(result.ok ? AppLogLevel::Info : AppLogLevel::Warning,
                result.ok ? "Task status update committed"
                          : "Task status update failed or was rolled back");
        } catch (...) {}
    }
    return result;
}

AppMutationResult DeleteTaskWithRecovery(const std::filesystem::path& directory,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const std::string& taskId, const std::string& actor,
    std::function<void(AppLogLevel, const std::string&)> eventLogger) {
    AppMutationResult result;
    const auto matches = std::count_if(tasks.begin(), tasks.end(), [&](const auto& task) { return task.id == taskId; });
    if (taskId.empty() || matches != 1) {
        result.errorMessage = u8"Задача не найдена или её ID неоднозначен.";
        return result;
    }
    const auto selected = std::find_if(tasks.begin(), tasks.end(), [&](const auto& task) { return task.id == taskId; });
    if (!selected->participants.empty()) {
        result.errorMessage = u8"По задаче уже начислен XP. Для безопасного отката нужен вариант удаления с контекстом профилей.";
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
            try { result.errorMessage += RecoverTaskCompletionWithNotice(directory); }
            catch (const std::exception&) { result.errorMessage += u8" Откат не завершён. Перезапустите Qt для восстановления журнала."; }
        }
    }
    emitOptionalEvent(eventLogger, result.ok ? AppLogLevel::Info : AppLogLevel::Warning,
        result.ok ? "Task deletion committed" : "Task deletion failed or was rolled back");
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
    if (selected->participants.empty()) {
        result = DeleteTaskWithRecovery(app.storageDir, tasks, audit, taskId, actor);
        emitTransactionOutcome(app, result.ok, "Task deletion committed", "Task deletion failed or was rolled back");
        return result;
    }

    std::set<std::string> profileIds;
    std::vector<Profile> rollbackProfiles;
    rollbackProfiles.reserve(selected->participants.size());
    for (const auto& participant : selected->participants) {
        if (!safeProfileId(participant.profileId) || !profileIds.insert(participant.profileId).second ||
            !app.storage.set_active_profile(participant.profileId)) {
            result.awardRollbackUnavailable = true;
            result.errorMessage = u8"Профиль участника недоступен или повторяется. Удаление остановлено."; return result;
        }
        auto profile = app.storage.load_profile();
        if (!profile || !ProfileMatchesTaskRollbackPostcondition(participant.rollbackSnapshot, *profile)) {
            result.awardRollbackUnavailable = true;
            result.errorMessage = u8"Профиль участника изменился после этой задачи или использует legacy snapshot. Новый прогресс нельзя откатывать."; return result;
        }
        Profile before = *profile;
        if (!ApplyProfileTaskRollbackSnapshot(participant.rollbackSnapshot, before)) {
            result.awardRollbackUnavailable = true;
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
            try { result.errorMessage += RecoverTaskCompletionWithNotice(app.storageDir); }
            catch (const std::exception&) { result.errorMessage += u8" Откат не завершён. Перезапустите Qt для восстановления журнала."; }
        }
    }
    emitTransactionOutcome(app, result.ok, "Task and awarded XP deletion committed",
                           "Task and awarded XP deletion failed or was rolled back");
    return result;
}

AppMutationResult DeleteAwardedTasksWithRecovery(AppContext& app,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const std::vector<std::string>& taskIds, const std::string& restoreProfileId, const std::string& actor) {
    AppMutationResult result;
    RestoreSelection restore{app.storage, restoreProfileId};
    if (taskIds.empty()) { result.errorMessage = u8"Выберите задачи для удаления."; return result; }

    std::set<std::string> requested;
    std::vector<const TaskEntry*> selectedTasks;
    selectedTasks.reserve(taskIds.size());
    for (const auto& id : taskIds) {
        if (id.empty() || !requested.insert(id).second) { result.errorMessage = u8"Список задач содержит пустой или повторный ID."; return result; }
        const auto count = std::count_if(tasks.begin(), tasks.end(), [&](const auto& task) { return task.id == id; });
        if (count != 1) { result.errorMessage = u8"Одна из задач не найдена или её ID неоднозначен."; return result; }
        selectedTasks.push_back(&*std::find_if(tasks.begin(), tasks.end(), [&](const auto& task) { return task.id == id; }));
    }

    std::map<std::string, std::vector<const TaskParticipant*>> snapshotsByProfile;
    for (const auto* task : selectedTasks) {
        std::set<std::string> taskProfiles;
        for (const auto& participant : task->participants) {
            if (!safeProfileId(participant.profileId) || participant.rollbackSnapshot.empty() ||
                !taskProfiles.insert(participant.profileId).second) {
                result.awardRollbackUnavailable = true;
                result.errorMessage = u8"Один из снимков XP повреждён или повторяет профиль; пакетное удаление отменено.";
                return result;
            }
            snapshotsByProfile[participant.profileId].push_back(&participant);
        }
    }

    std::map<std::string, Profile> rollbackProfiles;
    for (auto& [profileId, snapshots] : snapshotsByProfile) {
        if (!app.storage.set_active_profile(profileId)) {
            result.awardRollbackUnavailable = true;
            result.errorMessage = u8"Профиль участника недоступен. Пакетное удаление отменено.";
            return result;
        }
        const auto loaded = app.storage.load_profile();
        if (!loaded) {
            result.awardRollbackUnavailable = true;
            result.errorMessage = u8"Не удалось загрузить профиль участника для отката XP.";
            return result;
        }
        Profile current = *loaded;
        while (!snapshots.empty()) {
            size_t matchingIndex = snapshots.size();
            Profile rolledBack;
            std::string rolledBackBytes;
            for (size_t i = 0; i < snapshots.size(); ++i) {
                Profile candidate = current;
                if (!ProfileMatchesTaskRollbackPostcondition(snapshots[i]->rollbackSnapshot, current) ||
                    !ApplyProfileTaskRollbackSnapshot(snapshots[i]->rollbackSnapshot, candidate)) continue;
                const auto bytes = SerializeProfileTaskRollbackSnapshot(candidate);
                if (matchingIndex != snapshots.size() && bytes != rolledBackBytes) {
                    matchingIndex = snapshots.size();
                    break;
                }
                matchingIndex = i;
                rolledBack = std::move(candidate);
                rolledBackBytes = bytes;
            }
            if (matchingIndex == snapshots.size()) {
                result.awardRollbackUnavailable = true;
                result.errorMessage = u8"Текущий прогресс профиля не соответствует цепочке выбранных задач; ничего не удалено.";
                return result;
            }
            current = std::move(rolledBack);
            snapshots.erase(snapshots.begin() + static_cast<std::ptrdiff_t>(matchingIndex));
        }
        rollbackProfiles.emplace(profileId, std::move(current));
    }

    TaskCompletionPreview journal;
    std::set<std::string> journalProfiles;
    for (const auto& [profileId, unused] : rollbackProfiles) {
        (void)unused;
        if (journalProfiles.insert(profileId).second) journal.finalize.participants.push_back({profileId});
    }
    const auto oldTasks = tasks;
    const auto oldAudit = audit;
    bool prepared = false;
    try {
        prepareJournal(app.storageDir, journal);
        prepared = true;
        for (const auto& [profileId, profile] : rollbackProfiles) {
            if (!app.storage.set_active_profile(profileId) || !app.storage.save_profile(profile))
                throw std::runtime_error(u8"Не удалось сохранить пакетный откат профиля.");
        }
        result = AppDeleteTasksByIds(app.storageDir, tasks, taskIds, actor, &audit);
        if (!result.ok) throw std::runtime_error(result.errorMessage.empty() ? u8"Не удалось удалить выбранные задачи." : result.errorMessage);
        finishJournal(app.storageDir);
    } catch (const std::exception& error) {
        result = {};
        result.errorMessage = error.what();
        if (prepared) {
            tasks = oldTasks;
            audit = oldAudit;
            try { result.errorMessage += RecoverTaskCompletionWithNotice(app.storageDir); }
            catch (const std::exception&) { result.errorMessage += u8" Откат не завершён. Перезапустите Qt для восстановления журнала."; }
        }
    }
    emitTransactionOutcome(app, result.ok,
        "Bulk task deletion committed: " + std::to_string(result.changedCount),
        "Bulk task deletion failed or was rolled back");
    return result;
}

AppMutationResult DeleteAwardedTaskRecordKeepXpWithRecovery(const std::filesystem::path& directory,
    std::vector<TaskEntry>& tasks, std::vector<TaskAuditEntry>& audit,
    const std::string& taskId, const std::string& actor,
    std::function<void(AppLogLevel, const std::string&)> eventLogger) {
    AppMutationResult result;
    const auto matches = std::count_if(tasks.begin(), tasks.end(), [&](const auto& task) { return task.id == taskId; });
    if (taskId.empty() || matches != 1) {
        result.errorMessage = u8"Задача не найдена или её ID неоднозначен.";
        return result;
    }
    const auto selected = std::find_if(tasks.begin(), tasks.end(), [&](const auto& task) { return task.id == taskId; });
    if (selected->participants.empty()) {
        result.errorMessage = u8"У задачи нет начисленного XP для сохранения.";
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
        if (!AppAppendTaskAudit(directory, actor, taskId, "xp_disposition", u8"начислено по задаче",
                u8"сохранено в профилях; XP не изменён", &audit))
            throw std::runtime_error(u8"Не удалось записать в аудит, что начисленный XP сохранён.");
        finishJournal(directory);
    } catch (const std::exception& error) {
        result = {};
        result.errorMessage = error.what();
        if (prepared) {
            tasks = oldTasks;
            audit = oldAudit;
            try { result.errorMessage += RecoverTaskCompletionWithNotice(directory); }
            catch (const std::exception&) { result.errorMessage += u8" Откат не завершён. Перезапустите Qt для восстановления журнала."; }
        }
    }
    emitOptionalEvent(eventLogger, result.ok ? AppLogLevel::Info : AppLogLevel::Warning,
        result.ok ? "Task record deleted with awarded XP preserved"
                  : "Task record deletion failed or was rolled back");
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
    if (!preview.ok) {
        result.errorMessage = preview.errorMessage;
        emitCoreEvent(app, AppLogLevel::Error, "Task XP transaction failed or was rolled back");
        return result;
    }
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
        emitCoreEvent(app, AppLogLevel::Info, "Task XP transaction committed");
    } catch (const std::exception& e) {
        result = {};
        result.errorMessage = e.what();
        if (prepared) {
            tasks = oldTasks;
            audit = oldAudit;
            try {
                result.errorMessage += RecoverTaskCompletionWithNotice(
                    app.storageDir, u8"Начисление полностью отменено.");
            } catch (const std::exception&) {
                result.errorMessage += u8" Откат не завершён. Закройте Qt; журнал meta/qt-xp-transaction сохранён для восстановления при запуске.";
            }
        }
        emitCoreEvent(app, AppLogLevel::Error, "Task XP transaction failed or was rolled back");
    }
    return result;
}
