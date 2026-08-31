#include "AppShortcutsService.h"
#include "AppRecoveryStorage.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path ShortcutsStoragePath(const std::filesystem::path& storageDir) {
    return storageDir / "meta" / "shortcuts.json";
}

std::string EscapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04X", ch);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

std::int64_t CurrentUnixSeconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string GenerateShortcutId(std::int64_t nowSeconds, size_t index) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "shortcut_" << nowSeconds << "_" << index;
    return out.str();
}

bool IsValidIndex(const std::vector<ShortcutEntry>& shortcuts, int index) {
    return index >= 0 && index < static_cast<int>(shortcuts.size());
}

bool IsSymlinkOrUnreadable(const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    return std::filesystem::is_symlink(status) || (ec && ec != std::errc::no_such_file_or_directory);
}

} // namespace

bool AppSaveShortcutsData(const std::filesystem::path& storageDir,
                          const std::vector<ShortcutEntry>& shortcuts) {
    const auto target = ShortcutsStoragePath(storageDir);
    if (IsSymlinkOrUnreadable(target) || IsSymlinkOrUnreadable(target.parent_path())) return false;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "[\n";
    for (size_t i = 0; i < shortcuts.size(); ++i) {
        const auto& entry = shortcuts[i];
        out << "  {\"id\":\"" << EscapeJson(entry.id)
            << "\",\"label\":\"" << EscapeJson(entry.label)
            << "\",\"path\":\"" << EscapeJson(entry.path) << "\"}";
        if (i + 1 < shortcuts.size()) out << ",";
        out << "\n";
    }
    out << "]";
    return AppWriteUtf8BomWithRecovery(target, out.str());
}

AppShortcutMutationResult AppAddShortcut(const std::filesystem::path& storageDir,
                                         std::vector<ShortcutEntry>& shortcuts,
                                         const std::string& label,
                                         const std::string& path) {
    AppShortcutMutationResult result;
    if (path.empty()) {
        result.errorMessage = u8"Укажите путь к ярлыку.";
        return result;
    }
    const auto localPath = std::filesystem::u8path(path);
    if (!std::filesystem::exists(localPath)) {
        result.errorMessage = u8"Файл не найден: " + path;
        return result;
    }
    std::string finalLabel = label.empty() ? localPath.stem().u8string() : label;
    ShortcutEntry entry;
    entry.id = GenerateShortcutId(CurrentUnixSeconds(), shortcuts.size() + 1);
    entry.label = finalLabel;
    entry.path = path;
    std::vector<ShortcutEntry> backup = shortcuts;
    shortcuts.push_back(std::move(entry));
    if (!AppSaveShortcutsData(storageDir, shortcuts)) {
        shortcuts = std::move(backup);
        result.errorMessage = u8"Не удалось сохранить ярлык.";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.itemIndex = static_cast<int>(shortcuts.size()) - 1;
    return result;
}

AppShortcutMutationResult AppDeleteShortcut(const std::filesystem::path& storageDir,
                                            std::vector<ShortcutEntry>& shortcuts,
                                            int index) {
    AppShortcutMutationResult result;
    if (!IsValidIndex(shortcuts, index)) {
        result.errorMessage = u8"Ярлык не найден.";
        return result;
    }
    std::vector<ShortcutEntry> backup = shortcuts;
    shortcuts.erase(shortcuts.begin() + index);
    if (!AppSaveShortcutsData(storageDir, shortcuts)) {
        shortcuts = std::move(backup);
        result.errorMessage = u8"Не удалось сохранить ярлык.";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.itemIndex = shortcuts.empty() ? -1 : std::min(index, static_cast<int>(shortcuts.size()) - 1);
    return result;
}

AppShortcutMutationResult AppMoveShortcut(const std::filesystem::path& storageDir,
                                          std::vector<ShortcutEntry>& shortcuts,
                                          int fromIndex,
                                          int toIndex) {
    AppShortcutMutationResult result;
    if (!IsValidIndex(shortcuts, fromIndex) || !IsValidIndex(shortcuts, toIndex)) {
        result.errorMessage = u8"Ярлык не найден.";
        return result;
    }
    if (fromIndex == toIndex) {
        result.ok = true;
        result.itemIndex = toIndex;
        return result;
    }
    std::vector<ShortcutEntry> backup = shortcuts;
    std::swap(shortcuts[fromIndex], shortcuts[toIndex]);
    if (!AppSaveShortcutsData(storageDir, shortcuts)) {
        shortcuts = std::move(backup);
        result.errorMessage = u8"Не удалось сохранить порядок ярлыков.";
        return result;
    }
    result.ok = true;
    result.changed = true;
    result.itemIndex = toIndex;
    return result;
}
