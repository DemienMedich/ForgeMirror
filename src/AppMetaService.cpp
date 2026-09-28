#include "AppMetaService.h"
#include "AppWorkspaceStorageLock.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int ClampVaultRange(int value, int lo, int hi) {
    return std::clamp(value, lo, hi);
}

void emit(const AppMetaEventLogger& logger, AppLogLevel level, const char* message) noexcept {
    if (!logger) return;
    try { logger(level, message); } catch (...) {}
}

bool isBannerPayloadValid(std::string_view contents) {
    if (contents.size() >= 3 &&
        static_cast<unsigned char>(contents[0]) == 0xEF &&
        static_cast<unsigned char>(contents[1]) == 0xBB &&
        static_cast<unsigned char>(contents[2]) == 0xBF) {
        contents.remove_prefix(3);
    }
    std::size_t pos = 0;
    const auto skipWhitespace = [&] {
        while (pos < contents.size() && (contents[pos] == ' ' || contents[pos] == '\t' ||
               contents[pos] == '\r' || contents[pos] == '\n')) ++pos;
    };
    const auto consume = [&](char expected) {
        if (pos >= contents.size() || contents[pos] != expected) return false;
        ++pos;
        return true;
    };
    const auto parseText = [&] {
        if (!consume('"')) return false;
        bool hasContent = false;
        while (pos < contents.size()) {
            const unsigned char c = static_cast<unsigned char>(contents[pos++]);
            if (c == '"') return hasContent;
            if (c < 0x20) return false;
            if (c != '\\') {
                hasContent = true;
                continue;
            }
            if (pos >= contents.size()) return false;
            const char escaped = contents[pos++];
            if (escaped != '"' && escaped != '\\' && escaped != 'n' && escaped != 'r' && escaped != 't') return false;
            hasContent = true;
        }
        return false;
    };

    skipWhitespace();
    if (!consume('{')) return false;
    skipWhitespace();
    constexpr std::string_view itemsKey = "\"items\"";
    if (contents.substr(pos, itemsKey.size()) != itemsKey) return false;
    pos += itemsKey.size();
    skipWhitespace();
    if (!consume(':')) return false;
    skipWhitespace();
    if (!consume('[')) return false;
    skipWhitespace();
    if (!consume(']')) {
        while (true) {
            if (!parseText()) return false;
            skipWhitespace();
            if (consume(']')) break;
            if (!consume(',')) return false;
            skipWhitespace();
        }
    }
    skipWhitespace();
    if (!consume('}')) return false;
    skipWhitespace();
    return pos == contents.size();
}

bool loadLatestBannerTexts(const std::filesystem::path& storageDir, std::vector<std::string>& texts) {
    const auto path = storageDir / "meta" / "banner.json";
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) return false;
    if (!exists) { texts.clear(); return true; }
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad() || !isBannerPayloadValid(contents)) return false;
    texts = LoadBannerTexts(storageDir);
    return true;
}

bool loadLatestVault(const std::filesystem::path& storageDir, StorageVaultData& vault) {
    const auto path = StorageVaultPath(storageDir);
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) return false;
    if (!exists) return false;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad() || contents.empty() || contents.find('{') == std::string::npos) return false;
    if (contents.find("\"content_hash\"") != std::string::npos && !ValidateStorageVaultFileAtPath(path)) return false;
    vault = LoadStorageVault(storageDir);
    return true;
}

} // namespace

AppBannerMutationResult AppAddBannerText(const std::filesystem::path& storageDir,
                                         std::vector<std::string>& texts,
                                         const std::string& text,
                                         AppMetaEventLogger eventLogger) {
    AppBannerMutationResult result;
    if (text.empty()) {
        result.errorMessage = u8"Введите текст фразы.";
        emit(eventLogger, AppLogLevel::Warning, "Banner phrase creation rejected");
        return result;
    }
    AppWorkspaceStorageWriteLock writeLock(storageDir);
    if (!writeLock.acquired()) {
        result.errorMessage = u8"Не удалось заблокировать рабочее хранилище.";
        emit(eventLogger, AppLogLevel::Warning, "Banner phrase creation failed");
        return result;
    }
    const auto backup = texts;
    if (!loadLatestBannerTexts(storageDir, texts)) {
        texts = backup;
        result.errorMessage = u8"Не удалось прочитать сохранённые фразы.";
        emit(eventLogger, AppLogLevel::Error, "Banner phrase creation failed");
        return result;
    }
    texts.push_back(text);
    if (!SaveBannerTexts(storageDir, texts)) {
        texts = backup;
        result.errorMessage = u8"Не удалось сохранить баннер.";
        emit(eventLogger, AppLogLevel::Error, "Banner phrase creation failed");
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.itemIndex = static_cast<int>(texts.size()) - 1;
    emit(eventLogger, AppLogLevel::Info, "Banner phrase created");
    return result;
}

AppBannerMutationResult AppUpdateBannerText(const std::filesystem::path& storageDir,
                                            std::vector<std::string>& texts,
                                            int index,
                                            const std::string& text,
                                            AppMetaEventLogger eventLogger) {
    AppBannerMutationResult result;
    if (text.empty()) {
        result.errorMessage = u8"Введите текст фразы.";
        emit(eventLogger, AppLogLevel::Warning, "Banner phrase update rejected");
        return result;
    }
    AppWorkspaceStorageWriteLock writeLock(storageDir);
    if (!writeLock.acquired()) {
        result.errorMessage = u8"Не удалось заблокировать рабочее хранилище.";
        emit(eventLogger, AppLogLevel::Warning, "Banner phrase update failed");
        return result;
    }
    const auto backup = texts;
    std::vector<std::string> latest;
    if (!loadLatestBannerTexts(storageDir, latest)) {
        result.errorMessage = u8"Не удалось прочитать сохранённые фразы.";
        emit(eventLogger, AppLogLevel::Error, "Banner phrase update failed");
        return result;
    }
    if (latest != texts) {
        texts = std::move(latest);
        result.errorMessage = u8"Фразы изменились в другом месте. Обновите список и повторите действие.";
        emit(eventLogger, AppLogLevel::Warning, "Banner phrase update rejected after concurrent change");
        return result;
    }
    if (index < 0 || index >= static_cast<int>(texts.size())) {
        result.errorMessage = u8"Фраза не найдена.";
        emit(eventLogger, AppLogLevel::Warning, "Banner phrase update rejected");
        return result;
    }
    texts[index] = text;
    if (!SaveBannerTexts(storageDir, texts)) {
        texts = backup;
        result.errorMessage = u8"Не удалось сохранить баннер.";
        emit(eventLogger, AppLogLevel::Error, "Banner phrase update failed");
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.itemIndex = index;
    emit(eventLogger, AppLogLevel::Info, "Banner phrase updated");
    return result;
}

AppBannerMutationResult AppDeleteBannerText(const std::filesystem::path& storageDir,
                                            std::vector<std::string>& texts,
                                            int index,
                                            AppMetaEventLogger eventLogger) {
    AppBannerMutationResult result;
    AppWorkspaceStorageWriteLock writeLock(storageDir);
    if (!writeLock.acquired()) {
        result.errorMessage = u8"Не удалось заблокировать рабочее хранилище.";
        emit(eventLogger, AppLogLevel::Warning, "Banner phrase deletion failed");
        return result;
    }
    const auto backup = texts;
    std::vector<std::string> latest;
    if (!loadLatestBannerTexts(storageDir, latest)) {
        result.errorMessage = u8"Не удалось прочитать сохранённые фразы.";
        emit(eventLogger, AppLogLevel::Error, "Banner phrase deletion failed");
        return result;
    }
    if (latest != texts) {
        texts = std::move(latest);
        result.errorMessage = u8"Фразы изменились в другом месте. Обновите список и повторите действие.";
        emit(eventLogger, AppLogLevel::Warning, "Banner phrase deletion rejected after concurrent change");
        return result;
    }
    if (index < 0 || index >= static_cast<int>(texts.size())) {
        result.errorMessage = u8"Фраза не найдена.";
        emit(eventLogger, AppLogLevel::Warning, "Banner phrase deletion rejected");
        return result;
    }
    texts.erase(texts.begin() + index);
    if (!SaveBannerTexts(storageDir, texts)) {
        texts = backup;
        result.errorMessage = u8"Не удалось сохранить баннер.";
        emit(eventLogger, AppLogLevel::Error, "Banner phrase deletion failed");
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.itemIndex = texts.empty() ? -1 : std::min(index, static_cast<int>(texts.size()) - 1);
    emit(eventLogger, AppLogLevel::Info, "Banner phrase deleted");
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
                                          int pomodoroDaysMask,
                                          AppMetaEventLogger eventLogger) {
    AppVaultMutationResult result;
    AppWorkspaceStorageWriteLock writeLock(storageDir);
    if (!writeLock.acquired()) {
        result.errorMessage = u8"Не удалось заблокировать рабочее хранилище.";
        emit(eventLogger, AppLogLevel::Warning, "Vault settings update failed");
        return result;
    }
    StorageVaultData draft = vault;
    std::error_code ec;
    if (std::filesystem::exists(StorageVaultPath(storageDir), ec)) {
        if (ec || !loadLatestVault(storageDir, draft)) {
            result.errorMessage = u8"Не удалось прочитать сохранённое хранилище.";
            emit(eventLogger, AppLogLevel::Error, "Vault settings update failed");
            return result;
        }
    } else if (ec) {
        result.errorMessage = u8"Не удалось проверить сохранённое хранилище.";
        emit(eventLogger, AppLogLevel::Error, "Vault settings update failed");
        return result;
    }
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
        emit(eventLogger, AppLogLevel::Error, "Vault settings update failed");
        return result;
    }
    vault = LoadStorageVault(storageDir);
    result.ok = true;
    result.changed = true;
    emit(eventLogger, AppLogLevel::Info, "Vault settings updated");
    return result;
}
