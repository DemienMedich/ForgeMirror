#pragma once

#include <filesystem>
#include <string>

std::filesystem::path AppRecoveryBackupPath(const std::filesystem::path& path);

bool AppWriteUtf8BomWithRecovery(const std::filesystem::path& path,
                                 const std::string& payloadWithoutBom);

bool AppRestoreRecoveryBackup(const std::filesystem::path& path,
                              std::filesystem::path* damagedCopyPath = nullptr);

void AppSetRecoveryPrimaryWriteFailureForTests(bool enabled);
