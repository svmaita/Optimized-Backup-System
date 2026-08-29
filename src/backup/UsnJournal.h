#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

class UsnJournal {
public:
    static bool hasChanges(
        const std::filesystem::path& sourceFolder,
        const std::filesystem::path& backupFolder,
        bool& changesFound,
        uint64_t& knownFileCount);
    static bool markCurrent(
        const std::filesystem::path& sourceFolder,
        const std::filesystem::path& backupFolder,
        uint64_t fileCount);
    static bool collectChangedFiles(
        const std::filesystem::path& sourceFolder,
        const std::filesystem::path& backupFolder,
        std::vector<std::filesystem::path>& changedFiles,
        bool& deletionsDetected,
        uint64_t& knownFileCount);
};
