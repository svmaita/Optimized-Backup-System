#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct BackupStatistics {
    uint64_t totalFiles = 0;
    uint64_t newFiles = 0;
    uint64_t modifiedFiles = 0;
    uint64_t unchangedFiles = 0;
    uint64_t deletedFiles = 0;
    uint64_t copiedFiles = 0;
    uint64_t skippedFiles = 0;
    uint64_t failedFiles = 0;
    uint64_t totalSourceBytes = 0;
    uint64_t bytesCopied = 0;
    uint64_t bytesRead = 0;
    uint64_t bytesWritten = 0;
    double executionSeconds = 0.0;
    std::string summary;
    bool cancelled = false;
    bool showDeletedCount = false;
    std::string method;
};

struct BackupResult {
    BackupStatistics stats;
    std::vector<std::wstring> deletedFromSource;
    std::vector<std::wstring> warnings;
    std::vector<std::wstring> errors;
};
