#include "AppRecoveryStorage.h"

#include <chrono>
#include <fstream>
#include <iterator>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {

bool ReplaceFile(const std::filesystem::path& source, const std::filesystem::path& target) {
#ifdef _WIN32
    return MoveFileExW(source.c_str(), target.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    std::filesystem::rename(source, target, ec);
    return !ec;
#endif
}

bool WriteBinaryAtomic(const std::filesystem::path& path, const std::string& data) {
    std::error_code ec;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }

    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!out.good()) {
            out.close();
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }

    if (!ReplaceFile(tmp, path)) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

bool ReadBinary(const std::filesystem::path& path, std::string& data) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return in.good() || in.eof();
}

std::filesystem::path MakeDamagedCopyPath(const std::filesystem::path& path) {
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto directory = path.parent_path() / "updates";
    const std::string base = path.stem().string() + ".corrupt." + std::to_string(timestamp);
    std::filesystem::path candidate = directory / (base + path.extension().string());
    std::error_code ec;
    for (int suffix = 2; std::filesystem::exists(candidate, ec) && !ec; ++suffix) {
        candidate = directory / (base + "." + std::to_string(suffix) + path.extension().string());
    }
    return candidate;
}

} // namespace

std::filesystem::path AppRecoveryBackupPath(const std::filesystem::path& path) {
    return path.parent_path() / "updates" /
           (path.stem().string() + ".last-good" + path.extension().string());
}

bool AppWriteUtf8BomWithRecovery(const std::filesystem::path& path,
                                 const std::string& payloadWithoutBom) {
    static constexpr unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    std::string data(reinterpret_cast<const char*>(bom), sizeof(bom));
    data += payloadWithoutBom;

    // Commit the recovery copy first. A failed primary replace leaves the old primary intact.
    if (!WriteBinaryAtomic(AppRecoveryBackupPath(path), data)) return false;
    return WriteBinaryAtomic(path, data);
}

bool AppRestoreRecoveryBackup(const std::filesystem::path& path,
                              std::filesystem::path* damagedCopyPath) {
    if (damagedCopyPath) damagedCopyPath->clear();

    std::string recoveryData;
    if (!ReadBinary(AppRecoveryBackupPath(path), recoveryData)) return false;

    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !ec) {
        std::string damagedData;
        if (!ReadBinary(path, damagedData)) return false;
        const auto copyPath = MakeDamagedCopyPath(path);
        if (!WriteBinaryAtomic(copyPath, damagedData)) return false;
        if (damagedCopyPath) *damagedCopyPath = copyPath;
    }
    return WriteBinaryAtomic(path, recoveryData);
}
