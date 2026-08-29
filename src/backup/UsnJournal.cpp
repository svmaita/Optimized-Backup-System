#include "backup/UsnJournal.h"

#include <fstream>
#include <set>
#include <windows.h>
#include <winioctl.h>

namespace {
struct JournalState {
    unsigned long long journalId = 0;
    long long nextUsn = 0;
    unsigned long long fileCount = 0;
    std::wstring source;
};

std::filesystem::path statePath(const std::filesystem::path& backupFolder)
{
    return backupFolder / L".optimizedbackup.usn";
}

bool readState(const std::filesystem::path& path, JournalState& state)
{
    std::wifstream input(path);
    if (!input) return false;
    std::getline(input, state.source);
    return static_cast<bool>(input >> state.journalId >> state.nextUsn >> state.fileCount);
}

bool writeState(const std::filesystem::path& path, const JournalState& state)
{
    std::wofstream output(path, std::ios::trunc);
    if (!output) return false;
    output << state.source << L'\n' << state.journalId << L' ' << state.nextUsn << L' ' << state.fileCount << L'\n';
    return static_cast<bool>(output);
}

bool readJournalInfo(const std::filesystem::path& source, USN_JOURNAL_DATA_V0& journal)
{
    wchar_t volumeRoot[MAX_PATH]{};
    if (!GetVolumePathNameW(source.c_str(), volumeRoot, MAX_PATH)) return false;
    std::wstring volumeName = L"\\\\.\\" + std::wstring(volumeRoot, 0, 2);
    HANDLE volume = CreateFileW(volumeName.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (volume == INVALID_HANDLE_VALUE) return false;
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(volume, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journal, sizeof(journal), &bytes, nullptr);
    CloseHandle(volume);
    return ok != FALSE;
}

bool getChangesSince(const std::filesystem::path& source, const JournalState& state, bool& changed, USN_JOURNAL_DATA_V0& current)
{
    if (!readJournalInfo(source, current) || current.UsnJournalID != state.journalId || state.nextUsn < current.FirstUsn || state.nextUsn > current.NextUsn) return false;
    if (state.nextUsn == current.NextUsn) {
        changed = false;
        return true;
    }

    wchar_t volumeRoot[MAX_PATH]{};
    if (!GetVolumePathNameW(source.c_str(), volumeRoot, MAX_PATH)) return false;
    std::wstring volumeName = L"\\\\.\\" + std::wstring(volumeRoot, 0, 2);
    HANDLE volume = CreateFileW(volumeName.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (volume == INVALID_HANDLE_VALUE) return false;

    READ_USN_JOURNAL_DATA_V0 readData{};
    readData.StartUsn = state.nextUsn;
    readData.ReasonMask = USN_REASON_BASIC_INFO_CHANGE | USN_REASON_CLOSE | USN_REASON_DATA_EXTEND | USN_REASON_DATA_OVERWRITE | USN_REASON_RENAME_NEW_NAME | USN_REASON_RENAME_OLD_NAME | USN_REASON_FILE_DELETE;
    readData.ReturnOnlyOnClose = FALSE;
    readData.Timeout = 0;
    readData.BytesToWaitFor = 0;
    readData.UsnJournalID = state.journalId;
    char buffer[64 * 1024]{};
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(volume, FSCTL_READ_USN_JOURNAL, &readData, sizeof(readData), buffer, sizeof(buffer), &bytes, nullptr);
    if (ok && bytes >= sizeof(USN)) {
        changed = *reinterpret_cast<USN*>(buffer + sizeof(DWORD)) != state.nextUsn;
    }
    CloseHandle(volume);
    return ok != FALSE;
}
}

bool UsnJournal::hasChanges(const std::filesystem::path& sourceFolder, const std::filesystem::path& backupFolder, bool& changesFound, uint64_t& knownFileCount)
{
    JournalState state;
    if (!readState(statePath(backupFolder), state) || state.source != sourceFolder.wstring()) {
        changesFound = true;
        knownFileCount = 0;
        return false;
    }
    knownFileCount = state.fileCount;
    USN_JOURNAL_DATA_V0 current{};
    return getChangesSince(sourceFolder, state, changesFound, current);
}

bool UsnJournal::markCurrent(const std::filesystem::path& sourceFolder, const std::filesystem::path& backupFolder, uint64_t fileCount)
{
    USN_JOURNAL_DATA_V0 current{};
    if (!readJournalInfo(sourceFolder, current)) return false;
    JournalState state;
    state.source = sourceFolder.wstring();
    state.journalId = current.UsnJournalID;
    state.nextUsn = current.NextUsn;
    state.fileCount = fileCount;
    return writeState(statePath(backupFolder), state);
}

bool UsnJournal::collectChangedFiles(const std::filesystem::path& sourceFolder, const std::filesystem::path& backupFolder, std::vector<std::filesystem::path>& changedFiles, bool& deletionsDetected, uint64_t& knownFileCount)
{
    JournalState state;
    if (!readState(statePath(backupFolder), state) || state.source != sourceFolder.wstring()) return false;
    knownFileCount = state.fileCount;

    USN_JOURNAL_DATA_V0 current{};
    if (!readJournalInfo(sourceFolder, current) || current.UsnJournalID != state.journalId || state.nextUsn < current.FirstUsn || state.nextUsn > current.NextUsn) return false;
    if (state.nextUsn == current.NextUsn) return true;

    wchar_t volumeRoot[MAX_PATH]{};
    if (!GetVolumePathNameW(sourceFolder.c_str(), volumeRoot, MAX_PATH)) return false;
    std::wstring volumeName = L"\\\\.\\" + std::wstring(volumeRoot, 0, 2);
    HANDLE volume = CreateFileW(volumeName.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (volume == INVALID_HANDLE_VALUE) return false;

    READ_USN_JOURNAL_DATA_V0 readData{};
    readData.StartUsn = state.nextUsn;
    readData.ReasonMask = 0xFFFFFFFF;
    readData.UsnJournalID = state.journalId;
    char buffer[64 * 1024]{};
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(volume, FSCTL_READ_USN_JOURNAL, &readData, sizeof(readData), buffer, sizeof(buffer), &bytes, nullptr);
    if (ok && bytes > sizeof(USN)) {
        std::set<std::wstring> seenPaths;
        DWORD offset = sizeof(USN);
        while (offset < bytes) {
            auto* record = reinterpret_cast<USN_RECORD_V2*>(buffer + offset);
            if ((record->Reason & (USN_REASON_FILE_DELETE | USN_REASON_RENAME_OLD_NAME)) != 0) deletionsDetected = true;
            if ((record->Reason & (USN_REASON_DATA_EXTEND | USN_REASON_DATA_OVERWRITE | USN_REASON_BASIC_INFO_CHANGE | USN_REASON_FILE_CREATE | USN_REASON_RENAME_NEW_NAME)) != 0) {
                FILE_ID_DESCRIPTOR descriptor{};
                descriptor.dwSize = sizeof(descriptor);
                descriptor.Type = FileIdType;
                descriptor.FileId.QuadPart = static_cast<LONGLONG>(record->FileReferenceNumber);
                HANDLE file = OpenFileById(volume, &descriptor, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, 0);
                if (file != INVALID_HANDLE_VALUE) {
                    wchar_t pathBuffer[32768]{};
                    DWORD length = GetFinalPathNameByHandleW(file, pathBuffer, 32768, FILE_NAME_NORMALIZED);
                    CloseHandle(file);
                    if (length > 0 && length < 32768) {
                        std::wstring path(pathBuffer, length);
                        if (path.rfind(L"\\\\?\\", 0) == 0) path.erase(0, 4);
                        std::filesystem::path candidate(path);
                        std::error_code ec;
                        auto relative = std::filesystem::relative(candidate, sourceFolder, ec);
                        if (!ec && !relative.empty() && relative.native().front() != L'.' && seenPaths.insert(candidate.wstring()).second) changedFiles.push_back(candidate);
                    }
                }
            }
            if (record->RecordLength == 0) break;
            offset += record->RecordLength;
        }
    }
    CloseHandle(volume);
    return ok != FALSE;
}

