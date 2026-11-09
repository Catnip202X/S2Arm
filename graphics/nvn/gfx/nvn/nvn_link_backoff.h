// 显示链路降到低频档（30 / 20 Hz）的退避（link_sync 用；纯状态机，时间由调用方给，单测 graphics/nvn/tests/t_link_backoff.c）。
// 降档要求「同一周期稳 hold 秒」：进低频档后不到 2 秒就因周期表示不了被打回基础档，说明游戏在来回切，hold 翻倍（1→2→4… 封顶 64 秒）；
//   在低频档稳住 60 秒恢复 1 秒。11-04 金刚 Mac 按游戏间隔出帧：间隔 3 稳 1 秒降 20 Hz、一帧间隔 2 立即回 60 Hz，260 秒换档 96 次。
#pragma once
#include <math.h>
#include <stdint.h>

enum { LB_HOLD0 = 1, LB_HOLD_MAX = 64, LB_FLAP_S = 2, LB_RESET_S = 60 };   // 秒
#define LB_S 1000000000ull
typedef struct { uint64_t since, hold; } link_backoff;   // since：进入当前低频档的时刻（0 = 不在低频档）；hold：降档要求的稳定时长（0 = 初值）

static inline uint64_t lb_hold(const link_backoff *b) { return b->hold ? b->hold : LB_HOLD0 * LB_S; }
// 每帧调用：在低频档已稳住 60 秒，hold 恢复初值
static inline void lb_tick(link_backoff *b, uint64_t now) { if (b->since && now - b->since >= LB_RESET_S * LB_S) b->hold = LB_HOLD0 * LB_S; }
// 当前低频档表示不了本帧周期、被打回基础档：进低频档不到 2 秒算来回切，hold 翻倍
static inline void lb_forced_back(link_backoff *b, uint64_t now) {
    uint64_t h = lb_hold(b);
    if (b->since && now - b->since < LB_FLAP_S * LB_S) b->hold = h * 2 > LB_HOLD_MAX * LB_S ? LB_HOLD_MAX * LB_S : h * 2;
}
// 链路档实际改变时调用：进入低频档记时刻，离开清零
// 低频档只有 3（30 Hz）和 4（20 Hz）；5 是高刷新档（PR #38 复审：`>= 3` 会把它串进低频语义——退避、降档稳定时长、表示不了就打回）
static inline int lb_low(int mode) { return mode == 3 || mode == 4; }
static inline void lb_applied(link_backoff *b, int from, int to, uint64_t now) { if (lb_low(to) && !lb_low(from)) b->since = now; else if (!lb_low(to)) b->since = 0; }
// 该链路档能否用整数个刻度给出这个周期（周期 0 = 不限，任何档都算能）。只对低频档有意义：
//   高刷新档（5）按上限跑、夹到屏幕，游戏比刻度慢只是少拿刻度，从不「表示不了」（原先按 30 Hz 算，1/144 得 t = 0.208 → 一进就被打回）
static inline int lb_exact(int mode, double period) {
    if (period <= 0 || !lb_low(mode)) return 1;
    double t = period * (mode == 4 ? 20 : 30);
    return t >= 1 - 1e-6 && fabs(t - round(t)) < 1e-6;
}
