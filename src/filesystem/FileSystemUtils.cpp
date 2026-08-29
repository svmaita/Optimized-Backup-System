#include "filesystem/FileSystemUtils.h"

#include <fstream>
#include <iostream>
#include <system_error>

#ifndef _WIN32
#error This project is implemented for Windows.
#endif

#include <windows.h>

namespace {
std::wstring toWide(const std::string& value) {
    if (value.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (count <= 0) return L"";
    std::wstring result(count - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, &result[0], count);
    return result;
}
}

std::vector<FileRecord> FileSystemUtils::collectFilesRecursively(const fs::path& sourceFolder) {
    std::vector<FileRecord> files;

    if (!fs::exists(sourceFolder) || !fs::is_directory(sourceFolder)) {
        return files;
    }

    for (const auto& entry : fs::recursive_directory_iterator(sourceFolder, fs::directory_options::skip_permission_denied)) {
        try {
            const auto& path = entry.path();
            if (fs::is_directory(path)) {
                continue;
            }

            FileRecord record;
            record.absolutePath = path.wstring();
            record.relativePath = fs::relative(path, sourceFolder).wstring();
            record.fileSize = static_cast<uint64_t>(fs::file_size(path));
            record.lastWriteTime = fs::last_write_time(path);
            record.isDirectory = false;
            files.push_back(record);
        } catch (...) {
            // Ignore unreadable files and continue.
        }
    }

    return files;
}

bool FileSystemUtils::isSubPath(const fs::path& candidate, const fs::path& root) {
    auto c = fs::weakly_canonical(candidate);
    auto r = fs::weakly_canonical(root);
    std::error_code ec;
    auto [first, last] = std::mismatch(c.begin(), c.end(), r.begin(), r.end());
    if (first == c.end() && last == r.end()) return true;
    if (last == r.end()) return true;
    auto rel = fs::relative(c, r, ec);
    if (ec) return false;
    return rel.empty() || rel.native().front() != L'.';
}

bool FileSystemUtils::ensureDirectoryExists(const fs::path& directoryPath) {
    std::error_code ec;
    if (fs::exists(directoryPath)) {
        return fs::is_directory(directoryPath);
    }
    return fs::create_directories(directoryPath, ec);
}

bool FileSystemUtils::pathIsValid(const fs::path& path) {
    if (path.empty()) return false;
    std::error_code ec;
    if (fs::exists(path, ec)) {
        return fs::is_directory(path);
    }
    return true;
}

fs::path FileSystemUtils::makeDestinationPath(
    const fs::path& sourceRoot,
    const fs::path& backupRoot,
    const fs::path& relativeFilePath)
{
    auto relative = fs::path(relativeFilePath);
    return backupRoot / relative;
}

bool FileSystemUtils::copyFileWithWindowsApi(
    const fs::path& sourceFile,
    const fs::path& destinationFile,
    uint64_t& bytesCopied,
    std::wstring& errorMessage)
{
    bytesCopied = 0;

    const std::wstring wideSource = sourceFile.wstring();
    const std::wstring wideDestination = destinationFile.wstring();

    HANDLE hSource = CreateFileW(
        wideSource.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (hSource == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        char buffer[256] = {};
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, error, 0, buffer, sizeof(buffer), nullptr);
        errorMessage = L"CreateFile on source failed: " + toWide(buffer);
        return false;
    }

    HANDLE hDestination = CreateFileW(
        wideDestination.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (hDestination == INVALID_HANDLE_VALUE) {
        CloseHandle(hSource);
        DWORD error = GetLastError();
        char buffer[256] = {};
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, error, 0, buffer, sizeof(buffer), nullptr);
        errorMessage = L"CreateFile on destination failed: " + toWide(buffer);
        return false;
    }

    const size_t bufferSize = 64 * 1024;
    std::vector<char> buffer(bufferSize);

    BOOL ok = TRUE;
    while (ok) {
        DWORD bytesRead = 0;
        ok = ReadFile(hSource, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr);
        if (!ok && GetLastError() != ERROR_HANDLE_EOF) {
            DWORD error = GetLastError();
            char msg[256] = {};
            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, error, 0, msg, sizeof(msg), nullptr);
            errorMessage = L"ReadFile failed: " + toWide(msg);
            CloseHandle(hSource);
            CloseHandle(hDestination);
            return false;
        }

        if (bytesRead == 0) {
            break;
        }

        DWORD bytesWritten = 0;
        if (!WriteFile(hDestination, buffer.data(), bytesRead, &bytesWritten, nullptr) || bytesWritten != bytesRead) {
            DWORD error = GetLastError();
            char msg[256] = {};
            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, error, 0, msg, sizeof(msg), nullptr);
            errorMessage = L"WriteFile failed: " + toWide(msg);
            CloseHandle(hSource);
            CloseHandle(hDestination);
            return false;
        }

        bytesCopied += bytesWritten;
    }

    CloseHandle(hSource);
    CloseHandle(hDestination);
    return true;
}
