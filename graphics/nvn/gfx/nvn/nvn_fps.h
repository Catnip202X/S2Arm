// 用户帧率设置只加上限；不改游戏的交换间隔，也不解除游戏原有的锁帧。
// 上限三种取值（S2A_FPS）：0 / off = 不额外限帧；display = 跟着当前显示器刷新率走；24–1000 = 该帧率。
//   上限只拉长周期、不会让锁帧游戏变快（nvn_fps_period 取较大值）；唯一因此能超过 60 的，是游戏自己不锁帧（交换间隔 0）那一路，
//   见 graphics/nvn/gfx/nvn/hle_nvn.c present_itv。文档口径见 docs/画面设置.md。
#pragma once
#include <stdint.h>

#define NVN_FPS_MIN_HZ 24
#define NVN_FPS_MAX_HZ 1000

enum { NVN_FPS_OFF = 0, NVN_FPS_DISPLAY = 1, NVN_FPS_HZ = 2 };

int nvn_fps_limit(void);          // 已解析的上限 Hz：OFF → 0；HZ → 该值；DISPLAY → 当前显示速率（无窗口 / 读不到 = 0）
int nvn_fps_mode(void);           // 当前生效取值的种类（NVN_FPS_*）
int nvn_fps_requested(void);      // 原始请求折成一个数：OFF → 0，DISPLAY → -1，HZ → Hz（浮层 / 日志用）
int nvn_fps_set(int mode, int hz);   // 运行中改上限：0 = 已生效，-1 = 拒绝（越界保留旧值，调用方据返回值打红）
double nvn_fps_period(double period);   // 秒；取原周期与用户上限周期的较大值
uint64_t nvn_fps_period_ns(uint64_t period);   // 纳秒；0 上限逐值保持输入
