#include "nvn_fps.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 当前显示器的刷新率（graphics/nvn/gfx/nvn/nvn_mtl.m nvn_be_display_hz：无窗口 / 没链图形层时为 0）。
//   弱声明：本文件也编给不链 Metal 后端的单测，取不到符号等于「读不到」，DISPLAY 就按不限处理并说一次。
__attribute__((weak)) int nvn_be_display_hz(void);

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static _Atomic int g_mode = NVN_FPS_OFF;   // NVN_FPS_*
static _Atomic int g_hz;                   // NVN_FPS_HZ 时的目标帧率
static _Atomic int g_said_display;         // DISPLAY 但读不到屏幕时只报一次

// 解析一处取值：空 / 0 / off = 不限；display = 跟屏；24–1000 = 该帧率。其它一律不认。
static int parse(const char *e, int *mode, int *hz) {
    *mode = NVN_FPS_OFF; *hz = 0;
    if (!e || !*e || !strcmp(e, "0") || !strcmp(e, "off")) return 0;
    if (!strcmp(e, "display")) { *mode = NVN_FPS_DISPLAY; return 0; }
    char *end = NULL; long v = strtol(e, &end, 10);
    if (!end || end == e || *end || v < NVN_FPS_MIN_HZ || v > NVN_FPS_MAX_HZ) return -1;
    *mode = NVN_FPS_HZ; *hz = (int)v; return 0;
}
static const char *describe(int mode, int hz, char *buf, size_t n) {
    if (mode == NVN_FPS_HZ) snprintf(buf, n, "%d Hz", hz);
    else if (mode == NVN_FPS_DISPLAY) snprintf(buf, n, "display（跟显示器）");
    else snprintf(buf, n, "不限");
    return buf;
}
// 起局读一次环境（配置错误当场退出，与原先一致）；之后只认 nvn_fps_set（运行中改）。
static void fps_init(void) {
    const char *e = getenv("S2A_FPS"); int mode, hz;
    if (parse(e, &mode, &hz)) { fprintf(stderr, "[nvn] 【红】 S2A_FPS=%s：只接受 0 / off / display / %d–%d\n", e, NVN_FPS_MIN_HZ, NVN_FPS_MAX_HZ); abort(); }
    atomic_store(&g_mode, mode); atomic_store(&g_hz, hz);
    if (mode == NVN_FPS_OFF && (!e || !*e)) return;   // 没设 = 不限：不刷日志
    char d[64];
    fprintf(stderr, "[nvn] S2A_FPS=%s：直连额外帧率上限（只拉长周期，锁帧游戏照游戏节拍）；DM_PRESENT_HZ / DM_FPS 不绕过此上限\n", describe(mode, hz, d, sizeof d));
}
int nvn_fps_limit(void) {
    pthread_once(&g_once, fps_init);
    int mode = atomic_load(&g_mode);
    if (mode == NVN_FPS_HZ) return atomic_load(&g_hz);
    if (mode != NVN_FPS_DISPLAY) return 0;
    int hz = nvn_be_display_hz ? nvn_be_display_hz() : 0;
    if (hz <= 0 && !atomic_exchange(&g_said_display, 1)) fprintf(stderr, "[nvn] S2A_FPS=display：读不到显示器刷新率（没有窗口 / 没链图形层），本次按不限处理\n");
    return hz > 0 ? hz : 0;
}
int nvn_fps_mode(void) { pthread_once(&g_once, fps_init); return atomic_load(&g_mode); }
int nvn_fps_requested(void) {
    pthread_once(&g_once, fps_init);
    int mode = atomic_load(&g_mode);
    return mode == NVN_FPS_HZ ? atomic_load(&g_hz) : mode == NVN_FPS_DISPLAY ? -1 : 0;
}
int nvn_fps_set(int mode, int hz) {
    pthread_once(&g_once, fps_init);
    char a[64], b[64];
    int oldm = atomic_load(&g_mode), oldh = atomic_load(&g_hz);
    if (mode != NVN_FPS_OFF && mode != NVN_FPS_DISPLAY && mode != NVN_FPS_HZ) {
        fprintf(stderr, "[nvn] 【红】 帧率上限：取值种类 %d 不认，保留 %s\n", mode, describe(oldm, oldh, a, sizeof a));
        return -1;
    }
    if (mode == NVN_FPS_HZ && (hz < NVN_FPS_MIN_HZ || hz > NVN_FPS_MAX_HZ)) {
        fprintf(stderr, "[nvn] 【红】 帧率上限 %d Hz 不在 %d–%d，保留 %s\n", hz, NVN_FPS_MIN_HZ, NVN_FPS_MAX_HZ, describe(oldm, oldh, a, sizeof a));
        return -1;
    }
    int want_hz = mode == NVN_FPS_HZ ? hz : 0;
    if (oldm == mode && oldh == want_hz) return 0;
    atomic_store(&g_mode, mode); atomic_store(&g_hz, want_hz);
    atomic_store(&g_said_display, 0);   // 换成 display 后重新允许报一次「读不到」
    fprintf(stderr, "[nvn] 帧率上限 %s → %s\n", describe(oldm, oldh, a, sizeof a), describe(mode, want_hz, b, sizeof b));
    return 0;
}
double nvn_fps_period(double period) {
    int fps = nvn_fps_limit(); double limit = fps ? 1.0 / fps : 0;
    return period < limit ? limit : period;
}
uint64_t nvn_fps_period_ns(uint64_t period) {
    int fps = nvn_fps_limit(); uint64_t limit = fps ? (1000000000ull + fps - 1) / fps : 0;
    return period < limit ? limit : period;
}
