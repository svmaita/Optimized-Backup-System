#include "backup/BackupEngine.h"
#include "backup/UsnJournal.h"

#include <chrono>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

#ifndef _WIN32
#error This project is implemented for Windows.
#endif

#include <windows.h>

namespace {
std::string formatBytes(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < 4) {
        value /= 1024.0;
        ++unitIndex;
    }
    std::ostringstream stream;
    stream.precision(2);
    stream << std::fixed << value << " " << units[unitIndex];
    return stream.str();
}
}

BackupEngine::BackupEngine() = default;

bool BackupEngine::isMetadataChanged(const FileRecord& sourceFile, const fs::path& backupFile) {
    if (!fs::exists(backupFile)) {
        return true;
    }

    std::error_code ec;
    auto backupSize = static_cast<uint64_t>(fs::file_size(backupFile, ec));
    if (ec) {
        return true;
    }

    auto backupTime = fs::last_write_time(backupFile, ec);
    if (ec) {
        return true;
    }

    if (sourceFile.fileSize != backupSize) {
        return true;
    }

    return sourceFile.lastWriteTime != backupTime;
}

bool BackupEngine::copySingleFile(
    const fs::path& sourceFile,
    const fs::path& destinationFile,
    BackupStatistics& stats,
    std::wstring& errorMessage)
{
    uint64_t bytesCopied = 0;
    if (!FileSystemUtils::ensureDirectoryExists(destinationFile.parent_path())) {
        errorMessage = L"Failed to create destination directory.";
        return false;
    }

    if (!FileSystemUtils::copyFileWithWindowsApi(sourceFile, destinationFile, bytesCopied, errorMessage)) {
        return false;
    }

    std::error_code timestampError;
    fs::last_write_time(destinationFile, fs::last_write_time(sourceFile, timestampError), timestampError);
    if (timestampError) {
        errorMessage = L"Backup copied, but its modified time could not be preserved.";
        return false;
    }

    stats.bytesCopied += bytesCopied;
    stats.bytesWritten += bytesCopied;
    stats.copiedFiles += 1;
    return true;
}

void BackupEngine::collectDeletedFiles(
    const fs::path& backupFolder,
    const std::vector<FileRecord>& sourceFiles,
    std::vector<std::wstring>& deletedFiles)
{
    if (!fs::exists(backupFolder)) {
        return;
    }

    std::vector<fs::path> backupPaths;
    for (const auto& entry : fs::recursive_directory_iterator(backupFolder, fs::directory_options::skip_permission_denied)) {
        if (fs::is_regular_file(entry.path())) {
            backupPaths.push_back(entry.path());
        }
    }

    std::set<std::wstring> backupSet;
    for (const auto& b : backupPaths) {
        auto relative = fs::relative(b, backupFolder).wstring();
        backupSet.insert(relative);
    }

    std::set<std::wstring> sourceSet;
    for (const auto& file : sourceFiles) {
        sourceSet.insert(file.relativePath);
    }

    for (const auto& backupRelative : backupSet) {
        if (!sourceSet.count(backupRelative)) {
            deletedFiles.push_back(backupRelative);
        }
    }
}

void BackupEngine::removeDeletedFilesFromBackup(
    const fs::path& backupFolder,
    const std::vector<std::wstring>& deletedFiles)
{
    for (const auto& relative : deletedFiles) {
        const fs::path target = backupFolder / relative;
        std::error_code ec;
        fs::remove(target, ec);
    }
}

bool BackupEngine::processFullBackup(
    const fs::path& sourceFolder,
    const fs::path& backupFolder,
    BackupStatistics& stats,
    BackupResult& result,
    const ProgressCallback& progress,
    const std::atomic_bool* cancelRequested,
    bool keepDeletedFiles)
{
    std::vector<FileRecord> files = FileSystemUtils::collectFilesRecursively(sourceFolder);
    stats.totalFiles = files.size();

    for (const auto& file : files) {
        if (cancelRequested != nullptr && cancelRequested->load()) {
            stats.cancelled = true;
            break;
        }
        fs::path target = backupFolder / file.relativePath;
        if (!FileSystemUtils::ensureDirectoryExists(target.parent_path())) {
            result.errors.push_back(L"Could not create directory: " + target.parent_path().wstring());
            stats.failedFiles += 1;
            continue;
        }

        std::wstring errorMessage;
        if (!copySingleFile(file.absolutePath, target, stats, errorMessage)) {
            result.errors.push_back(L"Failed to copy: " + file.absolutePath + L"\nReason: " + errorMessage);
            stats.failedFiles += 1;
        }
        if (progress) progress(stats.copiedFiles + stats.skippedFiles, stats.totalFiles);
    }

    stats.newFiles = stats.copiedFiles;
    stats.unchangedFiles = 0;
    stats.modifiedFiles = 0;
    stats.skippedFiles = 0;
    stats.method = "Full backup: checked and copied every file";
    result.stats = stats;
    result.deletedFromSource.clear();
    collectDeletedFiles(backupFolder, files, result.deletedFromSource);
    stats.deletedFiles = result.deletedFromSource.size();
    if (!keepDeletedFiles) {
        removeDeletedFilesFromBackup(backupFolder, result.deletedFromSource);
    }
    result.stats = stats;

    return true;
}

bool BackupEngine::processIncrementalBackup(
    const fs::path& sourceFolder,
    const fs::path& backupFolder,
    BackupStatistics& stats,
    BackupResult& result,
    const ProgressCallback& progress,
    const std::atomic_bool* cancelRequested,
    bool keepDeletedFiles,
    const std::vector<FileRecord>* selectedFiles)
{
    std::vector<FileRecord> files = selectedFiles != nullptr ? *selectedFiles : FileSystemUtils::collectFilesRecursively(sourceFolder);
    stats.totalFiles = files.size();

    for (const auto& file : files) {
        if (cancelRequested != nullptr && cancelRequested->load()) {
            stats.cancelled = true;
            break;
        }
        fs::path target = backupFolder / file.relativePath;
        bool existsInBackup = fs::exists(target);

        if (!existsInBackup) {
            std::wstring errorMessage;
            if (!copySingleFile(file.absolutePath, target, stats, errorMessage)) {
                result.errors.push_back(L"Failed to copy new file: " + file.absolutePath + L"\nReason: " + errorMessage);
                stats.failedFiles += 1;
            } else {
                stats.newFiles += 1;
            }
            if (progress) progress(stats.copiedFiles + stats.skippedFiles, stats.totalFiles);
            continue;
        }

        if (isMetadataChanged(file, target)) {
            std::wstring errorMessage;
            if (!copySingleFile(file.absolutePath, target, stats, errorMessage)) {
                result.errors.push_back(L"Failed to copy modified file: " + file.absolutePath + L"\nReason: " + errorMessage);
                stats.failedFiles += 1;
            } else {
                stats.modifiedFiles += 1;
            }
        } else {
            stats.unchangedFiles += 1;
            stats.skippedFiles += 1;
        }
        if (progress) progress(stats.copiedFiles + stats.skippedFiles, stats.totalFiles);
    }

    stats.copiedFiles = stats.newFiles + stats.modifiedFiles;
    stats.totalSourceBytes = 0;
    for (const auto& file : files) {
        stats.totalSourceBytes += file.fileSize;
    }

    collectDeletedFiles(backupFolder, files, result.deletedFromSource);
    stats.deletedFiles = result.deletedFromSource.size();
    if (!keepDeletedFiles) {
        removeDeletedFilesFromBackup(backupFolder, result.deletedFromSource);
    }
    stats.method = selectedFiles != nullptr ? "Incremental backup: processed changed files only" : "Incremental backup: checked every file";
    result.stats = stats;
    return true;
}

BackupResult BackupEngine::runBackup(const fs::path& sourceFolder, const fs::path& backupFolder, BackupMode mode, bool keepDeletedFiles, const ProgressCallback& progress, const std::atomic_bool* cancelRequested) {
    BackupResult result;
    BackupStatistics stats;
    stats.showDeletedCount = (mode == BackupMode::Incremental && !keepDeletedFiles);

    auto start = std::chrono::high_resolution_clock::now();
    uint64_t journalFileCount = 0;

    if (!fs::exists(sourceFolder) || !fs::is_directory(sourceFolder)) {
        result.errors.push_back(L"Source folder does not exist or is not a directory.");
        return result;
    }

    if (!FileSystemUtils::ensureDirectoryExists(backupFolder)) {
        result.errors.push_back(L"Backup destination could not be created.");
        return result;
    }

    if (sourceFolder == backupFolder) {
        result.errors.push_back(L"Source and destination must be different folders.");
        return result;
    }

    if (mode == BackupMode::Full) {
        processFullBackup(sourceFolder, backupFolder, stats, result, progress, cancelRequested, keepDeletedFiles);
    } else {
        bool changesFound = true;
        uint64_t knownFileCount = 0;
        std::vector<fs::path> changedPaths;
        bool deletionsDetected = false;
        if (UsnJournal::collectChangedFiles(sourceFolder, backupFolder, changedPaths, deletionsDetected, knownFileCount) && !deletionsDetected && !changedPaths.empty()) {
            std::vector<FileRecord> changedFiles;
            for (const auto& path : changedPaths) {
                std::error_code ec;
                if (!fs::is_regular_file(path, ec)) continue;
                FileRecord record;
                record.absolutePath = path.wstring();
                record.relativePath = fs::relative(path, sourceFolder, ec).wstring();
                record.fileSize = static_cast<uint64_t>(fs::file_size(path, ec));
                record.lastWriteTime = fs::last_write_time(path, ec);
                if (!ec) changedFiles.push_back(record);
            }
            processIncrementalBackup(sourceFolder, backupFolder, stats, result, progress, cancelRequested, keepDeletedFiles, &changedFiles);
            stats.totalFiles = knownFileCount + stats.newFiles;
            stats.skippedFiles = knownFileCount > stats.modifiedFiles ? knownFileCount - stats.modifiedFiles : 0;
            stats.unchangedFiles = stats.skippedFiles;
            journalFileCount = knownFileCount + stats.newFiles;
        } else if (UsnJournal::hasChanges(sourceFolder, backupFolder, changesFound, knownFileCount) && !changesFound) {
            stats.totalFiles = knownFileCount;
            stats.unchangedFiles = knownFileCount;
            stats.skippedFiles = knownFileCount;
            stats.deletedFiles = 0;
            stats.method = "Incremental backup: no changes found";
            if (progress) progress(knownFileCount, knownFileCount);
        } else {
            processIncrementalBackup(sourceFolder, backupFolder, stats, result, progress, cancelRequested, keepDeletedFiles);
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration<double>(end - start).count();
    stats.executionSeconds = elapsed;
    result.stats = stats;

    std::ostringstream summary;
    summary << "Files scanned: " << stats.totalFiles
            << " | Files copied: " << stats.copiedFiles
            << " | Files skipped: " << stats.skippedFiles
            << " | New: " << stats.newFiles
            << " | Modified: " << stats.modifiedFiles
            << " | Deleted: " << stats.deletedFiles
            << " | Unchanged: " << stats.unchangedFiles
            << " | Data copied: " << formatBytes(stats.bytesCopied)
            << " | Time: " << elapsed << " s";
    result.stats.summary = summary.str();

    if (!stats.cancelled && result.errors.empty()) {
        UsnJournal::markCurrent(sourceFolder, backupFolder, journalFileCount != 0 ? journalFileCount : stats.totalFiles);
    }

    return result;
}
