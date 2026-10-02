#pragma once

#include <windows.h>
#include <atomic>

// 高精度动画调度器：专用工作线程 + 高分辨率 waitable timer。
// 线程只按目标帧率 PostMessage(WM_ANIM_TICK)，不直接触碰窗口。
// 无动画时线程阻塞在事件上，CPU 占用为 0。
class AnimPump {
public:
    AnimPump() = default;
    ~AnimPump();

    AnimPump(const AnimPump&) = delete;
    AnimPump& operator=(const AnimPump&) = delete;

    bool Start(HWND msgWnd, UINT tickMessage);
    void Stop();

    // 主线程调用：切换动画状态并设置目标帧间隔（100ns 单位）。
    void SetAnimating(bool active, LONGLONG interval100ns);

private:
    static DWORD WINAPI ThreadProc(LPVOID param);
    void Loop();

    HWND msgWnd_ = nullptr;
    UINT tickMsg_ = 0;

    HANDLE thread_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    HANDLE wakeEvent_ = nullptr;
    HANDLE timer_ = nullptr;

    std::atomic<bool> animating_{false};
    std::atomic<LONGLONG> interval100ns_{0};
    std::atomic<bool> quit_{false};
};
