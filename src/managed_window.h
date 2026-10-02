#pragma once

#include <windows.h>
#include <string>

#include "config.h"

// 单次动画结束后的诊断快照（仅在 DebugLog 开启时输出一次）
struct AnimStats {
    int monitorHz = 60;
    int durationMs = 0;
    int frames = 0;
    double avgFrameMs = 0.0;
    double maxFrameMs = 0.0;
    int droppedFrames = 0;
    bool valid = false;
};

// 单个受管窗口的状态机、四边几何与时间驱动滑动动画。
class ManagedWindow {
public:
    enum class State {
        Normal,         // 未停靠
        Dragging,       // 用户正在拖动
        DockedVisible,  // 已停靠且可见
        WaitingHide,    // 已满足隐藏前提，等待 HideDelay
        Hiding,         // 正在滑出
        Hidden,         // 已滑出，仅留少量边缘
        Showing,        // 正在滑入
        Ignored,        // 用户显式排除
        Invalid,        // HWND 已失效
    };

    ManagedWindow(HWND hwnd, const Settings* settings);

    HWND Handle() const { return hwnd_; }
    DockEdge Edge() const { return edge_; }
    State CurrentState() const { return state_; }
    int MonitorHz() const { return monitorHz_; }
    const AnimStats& LastAnimStats() const { return lastStats_; }

    bool IsDocked() const { return edge_ != DockEdge::None; }
    bool IsHidden() const { return state_ == State::Hidden; }
    bool IsAnimating() const { return state_ == State::Hiding || state_ == State::Showing; }
    bool IsValid() const { return IsWindow(hwnd_) != FALSE; }

    void SetDragging(bool on);

    // 依据当前窗口位置判断是否靠近某条边；命中则缓存几何并以动画滑向停靠位。
    // refreshHz 由调用方按该窗口所在显示器检测后传入。
    bool DockNear(const RECT& monitorRect, const std::wstring& device, UINT dpi, int refreshHz);

    void Undock();
    void RestoreNow();

    void BeginHide();
    void BeginShow();

    // 推进动画；nowQpc 为 QueryPerformanceCounter 当前值
    bool Tick(LONGLONG nowQpc);

    RECT HotZone() const;

    void NotifyMouseInside() { waitValid_ = false; }
    void NotifyMouseOutside(DWORD nowMs);
    bool ShouldHide(DWORD nowMs, int hideDelayMs) const;

    void Recompute(const RECT& monitorRect, const std::wstring& device, UINT dpi);

private:
    void CacheRects();
    void StartAnim(const RECT& target, bool toHidden, int durationMs);
    void PopulateStats();

    HWND hwnd_ = nullptr;
    const Settings* settings_ = nullptr;

    DockEdge edge_ = DockEdge::None;
    State state_ = State::Normal;

    RECT monitorRect_{};
    std::wstring monitorDevice_;
    UINT dpi_ = 96;
    int monitorHz_ = 60;

    RECT dockedRect_{};
    RECT hiddenRect_{};

    struct Anim {
        bool active = false;
        RECT from{};
        RECT to{};
        LONGLONG startQpc = 0;
        LONGLONG durationQpc = 0;
        bool toHidden = false;

        // 诊断采样（每帧只累加整数，不做 IO）
        LONGLONG lastFrameQpc = 0;
        LONGLONG expectedIntervalQpc = 0;
        LONGLONG sumIntervalQpc = 0;
        LONGLONG maxIntervalQpc = 0;
        int frameCount = 0;
        int intervalCount = 0;
        int droppedFrames = 0;
    } anim_;

    AnimStats lastStats_{};

    bool waitValid_ = false;
    DWORD waitStart_ = 0;
};
