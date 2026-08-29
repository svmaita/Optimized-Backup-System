#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct FileRecord {
    std::wstring relativePath;
    std::wstring absolutePath;
    uint64_t fileSize = 0;
    std::filesystem::file_time_type lastWriteTime;
    bool isDirectory = false;
};

class FileSystemUtils {
public:
    static std::vector<FileRecord> collectFilesRecursively(const fs::path& sourceFolder);
    static bool isSubPath(const fs::path& candidate, const fs::path& root);
    static bool copyFileWithWindowsApi(const fs::path& sourceFile, const fs::path& destinationFile, uint64_t& bytesCopied, std::wstring& errorMessage);
    static bool ensureDirectoryExists(const fs::path& directoryPath);
    static bool pathIsValid(const fs::path& path);
    static fs::path makeDestinationPath(const fs::path& sourceRoot, const fs::path& backupRoot, const fs::path& relativeFilePath);
};
