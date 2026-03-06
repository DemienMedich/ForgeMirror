#include "AppProfessionService.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <utility>
#include <vector>

#include "AppUtils.h"
#include "SkillCatalog.h"

namespace {

std::filesystem::path ProfessionsPath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "professions.txt";
}

std::int64_t CurrentUnixSeconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

bool WriteAllUtf8Bom(const std::filesystem::path& path, const std::string& payloadWithoutBom) {
    std::error_code ec;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    static constexpr unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    out.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    out.write(payloadWithoutBom.data(), static_cast<std::streamsize>(payloadWithoutBom.size()));
    return out.good();
}

void RestoreActiveProfile(IJobStorage& storage, const std::string& restoreProfileId) {
    if (!restoreProfileId.empty()) {
        storage.set_active_profile(restoreProfileId);
    }
}

struct SkillRollbackEntry {
    std::string id;
    std::vector<std::string> professions;
};

bool RollbackSkillBindings(SkillCatalog& catalog, const std::vector<SkillRollbackEntry>& rollback) {
    for (auto it = rollback.rbegin(); it != rollback.rend(); ++it) {
        const std::string name = catalog.display_name(it->id);
        if (name.empty() && !catalog.contains_id(it->id)) {
            continue;
        }
        if (!catalog.update_skill(it->id,
                                  name,
                                  catalog.weight(it->id),
                                  catalog.description(it->id),
                                  catalog.category(it->id),
                                  it->professions)) {
            return false;
        }
    }
    return true;
}

struct ProfileRollbackEntry {
    std::string id;
    std::string professionId;
};

bool RollbackProfileBindings(IJobStorage& storage,
                             const std::string& restoreProfileId,
                             const std::vector<ProfileRollbackEntry>& rollback) {
    for (auto it = rollback.rbegin(); it != rollback.rend(); ++it) {
        if (!storage.set_active_profile(it->id)) {
            RestoreActiveProfile(storage, restoreProfileId);
            return false;
        }
        auto profile = storage.load_profile();
        if (!profile) {
            RestoreActiveProfile(storage, restoreProfileId);
            return false;
        }
        profile->set_profession_id(it->professionId);
        if (!storage.save_profile(*profile)) {
            RestoreActiveProfile(storage, restoreProfileId);
            return false;
        }
    }
    RestoreActiveProfile(storage, restoreProfileId);
    return true;
}

} // namespace

bool AppSaveProfessionsData(const std::filesystem::path& storageDir,
                            const std::vector<ProfessionEntry>& professions) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    for (const auto& profession : professions) {
        if (profession.id.empty() || profession.name.empty()) continue;
        out << profession.id << "|" << profession.name << "|" << profession.description << "\n";
    }
    return WriteAllUtf8Bom(ProfessionsPath(storageDir), out.str());
}

std::string AppGenerateProfessionId(const std::vector<ProfessionEntry>& professions) {
    const std::int64_t now = CurrentUnixSeconds();
    int suffix = static_cast<int>(professions.size()) + 1;
    std::string base = "pr_" + std::to_string(now);
    auto exists = [&](const std::string& id) {
        return std::any_of(professions.begin(), professions.end(),
                           [&](const ProfessionEntry& item) { return item.id == id; });
    };
    std::string candidate = base;
    while (exists(candidate)) {
        candidate = base + "_" + std::to_string(suffix++);
    }
    return candidate;
}

AppProfessionMutationResult AppSaveProfessionEntry(const std::filesystem::path& storageDir,
                                                   std::vector<ProfessionEntry>& professions,
                                                   int editIndex,
                                                   const std::string& name,
                                                   const std::string& description) {
    AppProfessionMutationResult result;
    if (name.empty()) {
        result.errorMessage = u8"Название обязательно.";
        return result;
    }

    const std::vector<ProfessionEntry> backup = professions;
    int targetIndex = editIndex;
    if (targetIndex >= 0 && targetIndex < static_cast<int>(professions.size())) {
        professions[targetIndex].name = name;
        professions[targetIndex].description = description;
    } else {
        ProfessionEntry profession;
        profession.id = AppGenerateProfessionId(professions);
        profession.name = name;
        profession.description = description;
        professions.push_back(std::move(profession));
        targetIndex = static_cast<int>(professions.size()) - 1;
    }

    if (!AppSaveProfessionsData(storageDir, professions)) {
        professions = backup;
        result.errorMessage = u8"Не удалось сохранить professions.txt.";
        return result;
    }

    result.ok = true;
    result.changed = true;
    result.professionIndex = targetIndex;
    return result;
}

AppProfessionMutationResult AppAssignProfessionToProfile(IJobStorage& storage,
                                                         const std::string& restoreProfileId,
                                                         const std::string& profileId,
                                                         const std::string& professionId) {
    AppProfessionMutationResult result;
    if (!storage.set_active_profile(profileId)) {
        RestoreActiveProfile(storage, restoreProfileId);
        result.errorMessage = u8"Не удалось открыть профиль.";
        return result;
    }
    auto profile = storage.load_profile();
    if (!profile) {
        RestoreActiveProfile(storage, restoreProfileId);
        result.errorMessage = u8"Не удалось загрузить профиль.";
        return result;
    }
    const bool changed = profile->profession_id() != professionId;
    profile->set_profession_id(professionId);
    if (!storage.save_profile(*profile)) {
        RestoreActiveProfile(storage, restoreProfileId);
        result.errorMessage = u8"Не удалось сохранить профессию профиля.";
        return result;
    }
    RestoreActiveProfile(storage, restoreProfileId);
    result.ok = true;
    result.changed = changed;
    result.affectedProfiles = changed ? 1 : 0;
    return result;
}

AppProfessionMutationResult AppDeleteProfessionEntry(const std::filesystem::path& storageDir,
                                                     std::vector<ProfessionEntry>& professions,
                                                     IJobStorage& storage,
                                                     const std::vector<IJobStorage::ProfileInfo>& profiles,
                                                     SkillCatalog& catalog,
                                                     const std::string& restoreProfileId,
                                                     const std::string& professionId) {
    AppProfessionMutationResult result;
    if (professionId.empty()) {
        result.errorMessage = u8"Профессия не выбрана.";
        return result;
    }

    auto professionIt = std::find_if(professions.begin(), professions.end(),
                                     [&](const ProfessionEntry& item) { return item.id == professionId; });
    if (professionIt == professions.end()) {
        result.errorMessage = u8"Профессия не найдена.";
        return result;
    }

    std::vector<SkillRollbackEntry> skillRollback;
    for (const auto& skillId : catalog.skills()) {
        auto professionIds = catalog.professions(skillId);
        auto hit = std::find(professionIds.begin(), professionIds.end(), professionId);
        if (hit == professionIds.end()) continue;
        skillRollback.push_back({skillId, professionIds});
        professionIds.erase(hit);
        if (!catalog.update_skill(skillId,
                                  catalog.display_name(skillId),
                                  catalog.weight(skillId),
                                  catalog.description(skillId),
                                  catalog.category(skillId),
                                  professionIds)) {
            RollbackSkillBindings(catalog, skillRollback);
            result.errorMessage = u8"Не удалось обновить навыки профессии.";
            return result;
        }
    }

    std::vector<ProfileRollbackEntry> profileRollback;
    for (const auto& info : profiles) {
        if (info.archived) continue;
        if (!storage.set_active_profile(info.id)) {
            RollbackProfileBindings(storage, restoreProfileId, profileRollback);
            RollbackSkillBindings(catalog, skillRollback);
            result.errorMessage = u8"Не удалось открыть профиль для очистки профессии.";
            return result;
        }
        auto profile = storage.load_profile();
        if (!profile) {
            RollbackProfileBindings(storage, restoreProfileId, profileRollback);
            RollbackSkillBindings(catalog, skillRollback);
            result.errorMessage = u8"Не удалось загрузить профиль для очистки профессии.";
            return result;
        }
        if (profile->profession_id() != professionId) {
            continue;
        }
        profileRollback.push_back({info.id, professionId});
        profile->set_profession_id("");
        if (!storage.save_profile(*profile)) {
            RollbackProfileBindings(storage, restoreProfileId, profileRollback);
            RollbackSkillBindings(catalog, skillRollback);
            result.errorMessage = u8"Не удалось снять профессию с профиля.";
            return result;
        }
    }
    RestoreActiveProfile(storage, restoreProfileId);

    const std::vector<ProfessionEntry> backup = professions;
    professions.erase(std::remove_if(professions.begin(), professions.end(),
                                     [&](const ProfessionEntry& item) { return item.id == professionId; }),
                      professions.end());
    if (!AppSaveProfessionsData(storageDir, professions)) {
        professions = backup;
        RollbackProfileBindings(storage, restoreProfileId, profileRollback);
        RollbackSkillBindings(catalog, skillRollback);
        result.errorMessage = u8"Не удалось сохранить professions.txt.";
        return result;
    }

    result.ok = true;
    result.changed = true;
    result.affectedProfiles = static_cast<int>(profileRollback.size());
    result.affectedSkills = static_cast<int>(skillRollback.size());
    return result;
}
