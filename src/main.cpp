#include <windows.h>

#include "manager.h"
#include "tray.h"

namespace {

constexpr wchar_t kWindowClass[] = L"EdgeFlowMessageWindow";
constexpr UINT kTrayMessage = WM_APP + 1;

constexpr int kHotkeyToggleIgnore = 1;  // Ctrl+Alt+H
constexpr int kHotkeyShowAll = 2;       // Ctrl+Alt+S

constexpr UINT kMenuShowAll = 1001;
constexpr UINT kMenuToggleEnabled = 1002;
constexpr UINT kMenuClearAll = 1003;
constexpr UINT kMenuExit = 1004;

Manager* g_manager = nullptr;
Tray g_tray;
bool g_trayTipShown = false;

void ShowTrayMenu(HWND hwnd) {
    POINT pt{};
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;

    const bool enabled = (g_manager != nullptr) && g_manager->Enabled();

    AppendMenuW(menu, MF_STRING, kMenuShowAll, L"显示所有隐藏窗口");
    AppendMenuW(menu, MF_STRING | (enabled ? MF_CHECKED : 0), kMenuToggleEnabled,
                L"启用全局吸附");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuClearAll, L"清除当前会话的受管窗口");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"退出");

    SetForegroundWindow(hwnd);
    const UINT cmd = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
        case kMenuShowAll:
            if (g_manager != nullptr) g_manager->ShowAll();
            break;
        case kMenuToggleEnabled:
            if (g_manager != nullptr) g_manager->ToggleEnabled();
            break;
        case kMenuClearAll:
            if (g_manager != nullptr) g_manager->ClearAll();
            break;
        case kMenuExit:
            DestroyWindow(hwnd);
            break;
        default:
            break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g_tray.Add(hwnd, kTrayMessage,
                       L"EdgeFlow - 拖动窗口到屏幕边缘自动停靠 (Ctrl+Alt+H 排除, Ctrl+Alt+S 全部显示)");
            if (!RegisterHotKey(hwnd, kHotkeyToggleIgnore,
                                MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'H') ||
                !RegisterHotKey(hwnd, kHotkeyShowAll,
                                MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'S')) {
                MessageBoxW(nullptr,
                            L"全局快捷键 Ctrl+Alt+H / Ctrl+Alt+S 注册失败，可能已被其他程序占用。\n"
                            L"程序仍可通过托盘右键菜单使用。",
                            L"EdgeFlow", MB_OK | MB_ICONWARNING);
            }
            return 0;

        case WM_HOTKEY:
            if (g_manager == nullptr) return 0;
            if (wp == kHotkeyToggleIgnore) {
                g_manager->ToggleIgnoreForeground();
            } else if (wp == kHotkeyShowAll) {
                g_manager->ShowAll();
            }
            return 0;

        case WM_TIMER:
            if (wp == kManagerTimerId && g_manager != nullptr) {
                g_manager->OnTimer();
            }
            return 0;

        case WM_DOCK_EVENT:
            if (g_manager != nullptr) {
                g_manager->OnEvent(wp, lp);
            }
            return 0;

        case WM_ANIM_TICK:
            if (g_manager != nullptr) {
                g_manager->OnAnimationTick();
            }
            return 0;

        case kTrayMessage:
            if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
                ShowTrayMenu(hwnd);
            } else if (LOWORD(lp) == WM_LBUTTONUP && !g_trayTipShown) {
                g_trayTipShown = true;
                MessageBoxW(nullptr,
                            L"EdgeFlow 正在运行。\n\n"
                            L"把任意窗口拖到屏幕任意一边松手即可停靠。\n"
                            L"Ctrl+Alt+H: 排除/恢复当前窗口\n"
                            L"Ctrl+Alt+S: 显示所有隐藏窗口\n\n"
                            L"右键托盘图标可打开菜单。",
                            L"EdgeFlow", MB_OK | MB_ICONINFORMATION);
            }
            return 0;

        case WM_QUERYENDSESSION:
            if (g_manager != nullptr) {
                g_manager->Shutdown();
            }
            return TRUE;

        case WM_ENDSESSION:
            if (wp == TRUE && g_manager != nullptr) {
                g_manager->Shutdown();
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_manager != nullptr) {
                g_manager->Shutdown();
            }
            UnregisterHotKey(hwnd, kHotkeyToggleIgnore);
            UnregisterHotKey(hwnd, kHotkeyShowAll);
            g_tray.Remove();
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"EdgeFlow_SingleInstance");
    if (mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex != nullptr) CloseHandle(mutex);
        MessageBoxW(nullptr, L"EdgeFlow 已在运行。", L"EdgeFlow",
                    MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kWindowClass;
    if (RegisterClassExW(&wc) == 0) {
        CloseHandle(mutex);
        return 1;
    }

    HWND hwnd = CreateWindowExW(0, kWindowClass, L"EdgeFlow", 0,
                                0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
    if (hwnd == nullptr) {
        CloseHandle(mutex);
        return 1;
    }

    Manager manager(hwnd);
    g_manager = &manager;

    manager.LoadConfig();
    manager.Init();

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_manager = nullptr;
    CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}
