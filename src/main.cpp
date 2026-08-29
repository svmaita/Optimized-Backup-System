#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#include <memory>
#include <atomic>
#include <sstream>
#include <string>
#include <thread>

#include "backup/BackupEngine.h"

namespace {
constexpr UINT WM_BACKUP_FINISHED = WM_APP + 1;
constexpr int ID_SOURCE = 101;
constexpr int ID_DESTINATION = 102;
constexpr int ID_SOURCE_BROWSE = 103;
constexpr int ID_DESTINATION_BROWSE = 104;
constexpr int ID_FULL = 105;
constexpr int ID_INCREMENTAL = 106;
constexpr int ID_START = 107;
constexpr int ID_CANCEL = 108;
constexpr int ID_PROGRESS = 109;
constexpr int ID_KEEP_DELETED = 110;
constexpr int ID_REMOVE_DELETED = 111;

struct ProgressUpdate {
    uint64_t completed = 0;
    uint64_t total = 0;
};

struct AppState {
    HWND sourceEdit = nullptr;
    HWND destinationEdit = nullptr;
    HWND startButton = nullptr;
    HWND cancelButton = nullptr;
    HWND progressBar = nullptr;
    HWND statusLabel = nullptr;
    HWND results = nullptr;
    HWND fullRadio = nullptr;
    HWND incrementalRadio = nullptr;
    HWND keepDeletedRadio = nullptr;
    HWND removeDeletedRadio = nullptr;
    std::thread worker;
    std::atomic_bool cancelRequested{false};
};

void setText(HWND control, const std::wstring& text)
{
    SetWindowTextW(control, text.c_str());
}

std::wstring getText(HWND control)
{
    int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length), L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    return text;
}

std::wstring chooseFolder(HWND owner, const wchar_t* title)
{
    BROWSEINFOW info{};
    info.hwndOwner = owner;
    info.lpszTitle = title;
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
    if (item == nullptr) {
        return {};
    }
    wchar_t path[MAX_PATH]{};
    SHGetPathFromIDListW(item, path);
    CoTaskMemFree(item);
    return path;
}

void showResult(AppState* state, const BackupResult& result)
{
    const BackupStatistics& stats = result.stats;
    std::wstringstream output;
    output << L"Files scanned: " << stats.totalFiles << L"\r\n"
           << L"Files copied: " << stats.copiedFiles << L"\r\n"
           << L"Files skipped: " << stats.skippedFiles << L"\r\n"
           << L"New files: " << stats.newFiles << L"\r\n"
           << L"Modified files: " << stats.modifiedFiles << L"\r\n";
    if (stats.showDeletedCount && stats.deletedFiles > 0) {
        output << L"Deleted files: " << stats.deletedFiles << L"\r\n";
    }
    output << L"Unchanged files: " << stats.unchangedFiles << L"\r\n"
           << L"Data copied: " << stats.bytesCopied << L" bytes\r\n"
           << L"Execution time: " << stats.executionSeconds << L" seconds\r\n\r\n";
    if (!stats.summary.empty()) {
        output << std::wstring(stats.summary.begin(), stats.summary.end()) << L"\r\n";
    }
    for (const auto& error : result.errors) {
        output << L"Error: " << error << L"\r\n";
    }
    setText(state->results, output.str());
    if (result.stats.cancelled) {
        setText(state->statusLabel, L"Status: Backup cancelled.");
    } else {
        setText(state->statusLabel, result.errors.empty() ? L"Status: Backup completed." : L"Status: Completed with errors.");
    }
    EnableWindow(state->startButton, TRUE);
    EnableWindow(state->cancelButton, FALSE);
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_CREATE: {
        state = new AppState();
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        CreateWindowW(L"STATIC", L"Source Folder:", WS_CHILD | WS_VISIBLE, 20, 20, 140, 22, window, nullptr, nullptr, nullptr);
        state->sourceEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 170, 18, 500, 26, window, reinterpret_cast<HMENU>(ID_SOURCE), nullptr, nullptr);
        HWND sourceBrowse = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE, 680, 18, 100, 26, window, reinterpret_cast<HMENU>(ID_SOURCE_BROWSE), nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Destination Folder:", WS_CHILD | WS_VISIBLE, 20, 60, 140, 22, window, nullptr, nullptr, nullptr);
        state->destinationEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 170, 58, 500, 26, window, reinterpret_cast<HMENU>(ID_DESTINATION), nullptr, nullptr);
        HWND destinationBrowse = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE, 680, 58, 100, 26, window, reinterpret_cast<HMENU>(ID_DESTINATION_BROWSE), nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Backup Mode:", WS_CHILD | WS_VISIBLE, 20, 102, 140, 22, window, nullptr, nullptr, nullptr);
        state->fullRadio = CreateWindowW(L"BUTTON", L"Regular Backup", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, 170, 100, 130, 24, window, reinterpret_cast<HMENU>(ID_FULL), nullptr, nullptr);
        state->incrementalRadio = CreateWindowW(L"BUTTON", L"Incremental Backup", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 315, 100, 170, 24, window, reinterpret_cast<HMENU>(ID_INCREMENTAL), nullptr, nullptr);
        SendMessageW(state->fullRadio, BM_SETCHECK, BST_CHECKED, 0);

        HWND deletedFilesLabel = CreateWindowW(L"STATIC", L"Deleted Files:", WS_CHILD | WS_VISIBLE, 20, 136, 140, 22, window, nullptr, nullptr, nullptr);
        state->keepDeletedRadio = CreateWindowW(L"BUTTON", L"Keep Deleted Files", WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP, 170, 134, 170, 24, window, reinterpret_cast<HMENU>(ID_KEEP_DELETED), nullptr, nullptr);
        state->removeDeletedRadio = CreateWindowW(L"BUTTON", L"Remove Deleted Files", WS_CHILD | BS_AUTORADIOBUTTON, 350, 134, 180, 24, window, reinterpret_cast<HMENU>(ID_REMOVE_DELETED), nullptr, nullptr);
        SendMessageW(state->keepDeletedRadio, BM_SETCHECK, BST_CHECKED, 0);
        ShowWindow(state->keepDeletedRadio, SW_HIDE);
        ShowWindow(state->removeDeletedRadio, SW_HIDE);
        ShowWindow(deletedFilesLabel, SW_HIDE);

        state->startButton = CreateWindowW(L"BUTTON", L"Start Backup", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 620, 130, 160, 30, window, reinterpret_cast<HMENU>(ID_START), nullptr, nullptr);
        state->cancelButton = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE, 620, 165, 160, 30, window, reinterpret_cast<HMENU>(ID_CANCEL), nullptr, nullptr);
        EnableWindow(state->cancelButton, FALSE);
        state->statusLabel = CreateWindowW(L"STATIC", L"Status: Ready", WS_CHILD | WS_VISIBLE, 20, 170, 580, 24, window, nullptr, nullptr, nullptr);
        state->progressBar = CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 20, 198, 760, 22, window, reinterpret_cast<HMENU>(ID_PROGRESS), nullptr, nullptr);
        SendMessageW(state->progressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        state->results = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Choose both folders, then click Start Backup.", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 20, 235, 760, 230, window, nullptr, nullptr, nullptr);
        for (HWND control : {state->sourceEdit, state->destinationEdit, sourceBrowse, destinationBrowse, state->fullRadio, state->incrementalRadio, state->keepDeletedRadio, state->removeDeletedRadio, state->startButton, state->cancelButton, state->statusLabel, state->progressBar, state->results}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        return 0;
    }
    case WM_COMMAND:
        if (state == nullptr) return 0;
        switch (LOWORD(wParam)) {
        case ID_SOURCE_BROWSE:
            setText(state->sourceEdit, chooseFolder(window, L"Select Source Folder"));
            return 0;
        case ID_DESTINATION_BROWSE:
            setText(state->destinationEdit, chooseFolder(window, L"Select Backup Folder"));
            return 0;
        case ID_FULL:
        case ID_INCREMENTAL: {
            const bool fullBackup = SendMessageW(state->fullRadio, BM_GETCHECK, 0, 0) == BST_CHECKED;
            HWND deletedLabel = GetDlgItem(window, ID_KEEP_DELETED);
            HWND keepButton = state->keepDeletedRadio;
            HWND removeButton = state->removeDeletedRadio;
            if (deletedLabel != nullptr) {
                ShowWindow(deletedLabel, fullBackup ? SW_HIDE : SW_SHOW);
            }
            if (keepButton != nullptr) {
                ShowWindow(keepButton, fullBackup ? SW_HIDE : SW_SHOW);
            }
            if (removeButton != nullptr) {
                ShowWindow(removeButton, fullBackup ? SW_HIDE : SW_SHOW);
            }
            return 0;
        }
        case ID_START: {
            const std::wstring source = getText(state->sourceEdit);
            const std::wstring destination = getText(state->destinationEdit);
            if (source.empty() || destination.empty()) {
                MessageBoxW(window, L"Please Choose Both Folders.", L"Missing Folder", MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (source == destination) {
                MessageBoxW(window, L"The Source And Backup Folders Must Be Different.", L"Invalid Selection", MB_OK | MB_ICONWARNING);
                return 0;
            }
            const bool fullBackup = SendMessageW(state->fullRadio, BM_GETCHECK, 0, 0) == BST_CHECKED;
            const bool keepDeletedFiles = fullBackup || SendMessageW(state->keepDeletedRadio, BM_GETCHECK, 0, 0) == BST_CHECKED;
            setText(state->statusLabel, L"Status: Running backup...");
            setText(state->results, L"Starting backup...\r\n");
            SendMessageW(state->progressBar, PBM_SETPOS, 0, 0);
            EnableWindow(state->startButton, FALSE);
            EnableWindow(state->cancelButton, TRUE);
            state->cancelRequested.store(false);
            state->worker = std::thread([window, source, destination, fullBackup, keepDeletedFiles, state]() {
                BackupEngine engine;
                auto progress = [window](uint64_t completed, uint64_t total) {
                    auto* update = new ProgressUpdate{completed, total};
                    PostMessageW(window, WM_APP + 2, 0, reinterpret_cast<LPARAM>(update));
                };
                auto* result = new BackupResult(engine.runBackup(source, destination, fullBackup ? BackupMode::Full : BackupMode::Incremental, keepDeletedFiles, progress, &state->cancelRequested));
                PostMessageW(window, WM_BACKUP_FINISHED, 0, reinterpret_cast<LPARAM>(result));
            });
            return 0;
        }
        case ID_CANCEL:
            if (MessageBoxW(window, L"Cancel the backup? Files already copied will remain in the backup folder.", L"Confirm cancellation", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                state->cancelRequested.store(true);
                setText(state->statusLabel, L"Status: Cancelling...");
                EnableWindow(state->cancelButton, FALSE);
            }
            return 0;
        default:
            break;
        }
        break;
    case WM_BACKUP_FINISHED:
        if (state != nullptr) {
            std::unique_ptr<BackupResult> result(reinterpret_cast<BackupResult*>(lParam));
            if (state->worker.joinable()) state->worker.join();
            showResult(state, *result);
        }
        return 0;
    case WM_APP + 2:
        if (state != nullptr) {
            std::unique_ptr<ProgressUpdate> update(reinterpret_cast<ProgressUpdate*>(lParam));
            if (update->total > 0) {
                int percent = static_cast<int>((update->completed * 100) / update->total);
                SendMessageW(state->progressBar, PBM_SETPOS, percent, 0);
                std::wstringstream status;
                status << L"Status: Running backup... " << update->completed << L" of " << update->total << L" files";
                setText(state->statusLabel, status.str());
            }
        }
        return 0;
    case WM_DESTROY:
        if (state != nullptr) {
            if (state->worker.joinable()) state->worker.join();
            delete state;
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const wchar_t className[] = L"OptimizedBackupWindow";
    WNDCLASSW windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(IDC_ARROW));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&windowClass);
    HWND window = CreateWindowExW(0, className, L"Optimized Incremental Backup System", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 805, 540, nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        CoUninitialize();
        return 1;
    }
    ShowWindow(window, showCommand);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
