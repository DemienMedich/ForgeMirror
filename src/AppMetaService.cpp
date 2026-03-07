#include "AppMetaService.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace {

int ClampVaultRange(int value, int lo, int hi) {
    return std::clamp(value, lo, hi);
}

} // namespace

AppBannerMutationResult AppAddBannerText(const std::filesystem::path& storageDir,
                                         std::vector<std::string>& texts,
                                         const std::string& text) {
    AppBannerMutationResult result;
    if (text.empty()) {
        result.errorMessage = u8"Введите текст фразы.";
        return result;
    }
    std::vector<std::string> backup = texts;
    texts.push_back(text);
    if (!SaveBannerTexts(storageDir, texts)) {
        texts = std::move(backup);
        result.errorMessage = u8"Не удалось сохранить баннер.";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.itemIndex = static_cast<int>(texts.size()) - 1;
    return result;
}

AppBannerMutationResult AppUpdateBannerText(const std::filesystem::path& storageDir,
                                            std::vector<std::string>& texts,
                                            int index,
                                            const std::string& text) {
    AppBannerMutationResult result;
    if (text.empty()) {
        result.errorMessage = u8"Введите текст фразы.";
        return result;
    }
    if (index < 0 || index >= static_cast<int>(texts.size())) {
        result.errorMessage = u8"Фраза не найдена.";
        return result;
    }
    std::vector<std::string> backup = texts;
    texts[index] = text;
    if (!SaveBannerTexts(storageDir, texts)) {
        texts = std::move(backup);
        result.errorMessage = u8"Не удалось сохранить баннер.";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.itemIndex = index;
    return result;
}

AppBannerMutationResult AppDeleteBannerText(const std::filesystem::path& storageDir,
                                            std::vector<std::string>& texts,
                                            int index) {
    AppBannerMutationResult result;
    if (index < 0 || index >= static_cast<int>(texts.size())) {
        result.errorMessage = u8"Фраза не найдена.";
        return result;
    }
    std::vector<std::string> backup = texts;
    texts.erase(texts.begin() + index);
    if (!SaveBannerTexts(storageDir, texts)) {
        texts = std::move(backup);
        result.errorMessage = u8"Не удалось сохранить баннер.";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.itemIndex = texts.empty() ? -1 : std::min(index, static_cast<int>(texts.size()) - 1);
    return result;
}

AppVaultMutationResult AppApplyVaultDraft(const std::filesystem::path& storageDir,
                                          StorageVaultData& vault,
                                          const std::string& currencyName,
                                          const std::string& currencyCode,
                                          int logLimit,
                                          int pomodoroStartMinutes,
                                          int pomodoroEndMinutes,
                                          int pomodoroMinMinutes,
                                          int pomodoroCoinsPerCycle,
                                          int pomodoroDaysMask) {
    AppVaultMutationResult result;
    StorageVaultData draft = vault;
    draft.currencyName = currencyName;
    draft.currencyCode = currencyCode;
    draft.logLimit = ClampVaultRange(logLimit, 10, 50);
    draft.pomodoroStartMinutes = ClampVaultRange(pomodoroStartMinutes, 0, 24 * 60 - 1);
    draft.pomodoroEndMinutes = ClampVaultRange(pomodoroEndMinutes, 0, 24 * 60 - 1);
    draft.pomodoroMinMinutes = ClampVaultRange(pomodoroMinMinutes, 1, 90);
    draft.pomodoroCoinsPerCycle = ClampVaultRange(pomodoroCoinsPerCycle, 0, 5);
    draft.pomodoroDaysMask = pomodoroDaysMask;
    if (!SaveStorageVault(storageDir, draft)) {
        result.errorMessage = u8"Не удалось сохранить хранилище.";
        return result;
    }
    vault = LoadStorageVault(storageDir);
    result.ok = true;
    result.changed = true;
    return result;
}
