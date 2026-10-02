#pragma once

#include <windows.h>
#include <memory>
#include <string>
#include <vector>

#include "anim_pump.h"
#include "config.h"
#include "managed_window.h"

// 与 main.cpp 约定的定时器 ID 和事件消息
constexpr UINT_PTR kManagerTimerId = 1;
constexpr UINT WM_DOCK_EVENT = WM_APP + 10;
constexpr UINT WM_ANIM_TICK = WM_APP + 11;

enum : WPARAM {
    kEvtMoveStart = 1,
    kEvtMoveEnd = 2,
    kEvtForeground = 3,
    kEvtDestroy = 4,
};

// 全局受管窗口集合：自动识别、四边停靠、隐藏/展开调度、全屏保护。
class Manager {
public:
    explicit Manager(HWND messageWindow);
    ~Manager();

    void Init();
    void Shutdown();

    void LoadConfig();
    void SaveConfig();

    void ToggleIgnoreForeground();  // Ctrl+Alt+H
    void ShowAll();                 // Ctrl+Alt+S
    void ClearAll();

    void OnTimer();
    void OnEvent(WPARAM evt, LPARAM lParam);
    void OnAnimationTick();  // 由 WM_ANIM_TICK 触发，高精度动画推进

    const Settings& GetSettings() const { return settings_; }
    void ToggleEnabled();
    bool Enabled() const { return settings_.enabled; }

private:
    ManagedWindow* Find(HWND hwnd);
    void AddForDrag(HWND hwnd);
    void Remove(HWND hwnd);
    void PurgeInvalid();
    void SyncAnimPump();

    bool IsEligible(HWND hwnd) const;
    bool IsBlacklisted(HWND hwnd) const;
    bool IsIgnored(HWND hwnd) const;
    bool IsFullscreenForeground() const;

    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                      LONG idObject, LONG idChild, DWORD thread, DWORD time);

    inline static Manager* s_instance_ = nullptr;

    HWND msgWnd_ = nullptr;
    std::vector<HWINEVENTHOOK> hooks_;
    std::vector<std::unique_ptr<ManagedWindow>> windows_;
    Settings settings_;
    std::wstring configPath_;
    std::wstring logPath_;
    bool fullscreenGuardActive_ = false;

    AnimPump animPump_;
    bool animPumpActive_ = false;
    LONGLONG animPumpInterval100ns_ = 0;
};
