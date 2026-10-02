#pragma once

#include <windows.h>
#include <shellapi.h>

// 托盘图标（仅作为入口，不做 Dock / 任务栏替代）
class Tray {
public:
    bool Add(HWND hwnd, UINT callbackMessage, const wchar_t* tip);
    void Remove();

private:
    bool added_ = false;
    NOTIFYICONDATAW nid_{};
};
