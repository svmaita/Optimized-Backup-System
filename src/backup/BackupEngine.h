#pragma once

#include <filesystem>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "backup/BackupStatistics.h"
#include "filesystem/FileSystemUtils.h"

namespace fs = std::filesystem;

enum class BackupMode {
    Full,
    Incremental
};

class BackupEngine {
public:
    using ProgressCallback = std::function<void(uint64_t completedFiles, uint64_t totalFiles)>;

    BackupEngine();

    BackupResult runBackup(
        const fs::path& sourceFolder,
        const fs::path& backupFolder,
        BackupMode mode,
        bool keepDeletedFiles = true,
        const ProgressCallback& progress = {},
        const std::atomic_bool* cancelRequested = nullptr);
    static bool isMetadataChanged(const FileRecord& sourceFile, const fs::path& backupFile);

private:
    std::vector<std::wstring> m_warnings;
    std::vector<std::wstring> m_errors;

    bool copySingleFile(const fs::path& sourceFile, const fs::path& destinationFile, BackupStatistics& stats, std::wstring& errorMessage);
    bool processFullBackup(const fs::path& sourceFolder, const fs::path& backupFolder, BackupStatistics& stats, BackupResult& result, const ProgressCallback& progress, const std::atomic_bool* cancelRequested, bool keepDeletedFiles);
    bool processIncrementalBackup(const fs::path& sourceFolder, const fs::path& backupFolder, BackupStatistics& stats, BackupResult& result, const ProgressCallback& progress, const std::atomic_bool* cancelRequested, bool keepDeletedFiles, const std::vector<FileRecord>* selectedFiles = nullptr);
    void collectDeletedFiles(const fs::path& backupFolder, const std::vector<FileRecord>& sourceFiles, std::vector<std::wstring>& deletedFiles);
    void removeDeletedFilesFromBackup(const fs::path& backupFolder, const std::vector<std::wstring>& deletedFiles);
};
