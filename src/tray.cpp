#include "tray.h"

#include "resource.h"

bool Tray::Add(HWND hwnd, UINT callbackMessage, const wchar_t* tip) {
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd;
    nid_.uID = 1;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid_.uCallbackMessage = callbackMessage;

    // 使用编译进 EXE 的正式图标；加载失败才回退到系统默认图标
    HINSTANCE inst = GetModuleHandleW(nullptr);
    HICON icon = static_cast<HICON>(
        LoadImageW(inst, MAKEINTRESOURCEW(IDI_EDGEFLOW_APP), IMAGE_ICON,
                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    if (icon == nullptr) {
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    nid_.hIcon = icon;

    wcsncpy_s(nid_.szTip, tip, _TRUNCATE);

    added_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
    return added_;
}

void Tray::Remove() {
    if (!added_) return;
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    added_ = false;
}
