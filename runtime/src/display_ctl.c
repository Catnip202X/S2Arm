// 运行中改显示参数：<S2A_DOCS>/display.txt（口径见 runtime/include/s2a_display_ctl.h 与 docs/画面设置.md）。
//   只在帧线程调（ap_host.c s2a_frame_presented），内部 500 ms 限频；不动 g_sl / 表格写入线程，也不在帧路径上等锁。
#include "native.h"
#include "../include/s2a_display_ctl.h"
#include "../include/s2a_docs.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void s2a_fps_set_enabled(int on);   // runtime/src/fps.c（60 帧表热切；起局关的局也能运行中开，出图间隔除外）

// ---- 解析（纯函数，单测直接调） ----
static const char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
    return s;
}
// "WxH"，两个数都在 1–16384（同上屏路径的逐轴上限）
static int parse_size(const char *v, int *w, int *h) {
    int a = 0, b = 0; char tail = 0;
    if (sscanf(v, "%dx%d%c", &a, &b, &tail) != 2) return -1;
    if (a < 1 || b < 1 || a > 16384 || b > 16384) return -1;
    *w = a; *h = b; return 0;
}
static int parse_refresh(const char *v, int *out) {
    if (!strcmp(v, "off")) { *out = 0; return 0; }
    if (!strcmp(v, "display")) { *out = -1; return 0; }
    char tail = 0; int hz = 0;
    if (sscanf(v, "%d%c", &hz, &tail) != 1) return -1;
    if (hz < 24 || hz > 1000) return -1;
    *out = hz; return 0;
}
int s2a_display_parse(const char *text, s2a_display_cmd *c, int *bad_line, const char **bad_why) {
    if (bad_line) *bad_line = 0;
    if (bad_why) *bad_why = NULL;
    if (!c) return 0;
    c->refresh = -2; c->out_kind = 0; c->out_w = c->out_h = 0; c->fps60 = -1;
    if (!text) return 0;
    char buf[S2A_DISPLAY_MAX];
    snprintf(buf, sizeof buf, "%s", text);
    int n = 0, line = 0;
    for (char *p = buf; p && *p; ) {
        char *e = strchr(p, '\n');
        if (e) *e = 0;
        line++;
        char *hash = strchr(p, '#');
        if (hash) *hash = 0;
        const char *s = trim(p);
        if (*s) {
            char *eq = strchr((char *)s, '=');
            if (!eq) { if (bad_line && !*bad_line) { *bad_line = line; if (bad_why) *bad_why = "没有 ="; } }
            else {
                *eq = 0;
                const char *k = trim((char *)s), *v = trim(eq + 1);
                int ok = -1, known = 1;
                if (!strcmp(k, "refresh")) ok = parse_refresh(v, &c->refresh);
                else if (!strcmp(k, "out")) {
                    if (!strcmp(v, "native")) { c->out_kind = 1; c->out_w = c->out_h = 0; ok = 0; }
                    else if (!strcmp(v, "fit")) { c->out_kind = 2; c->out_w = c->out_h = 0; ok = 0; }
                    else if (!parse_size(v, &c->out_w, &c->out_h)) { c->out_kind = 3; ok = 0; }
                } else if (!strcmp(k, "fps60")) {
                    if (!strcmp(v, "0")) { c->fps60 = 0; ok = 0; }
                    else if (!strcmp(v, "1")) { c->fps60 = 1; ok = 0; }
                } else known = 0;   // 不认识的键：忽略（启动器 / 后续版本可以加，不当作坏行）
                if (known) {   // 不认识的键不进这里（也不进坏行）；这里不能 continue：跳过了行指针推进会死循环
                    if (!ok) n++;
                    else if (bad_line && !*bad_line) { *bad_line = line; if (bad_why) *bad_why = "值不认"; }
                }
            }
        }
        p = e ? e + 1 : NULL;
    }
    return n;
}

// ---- 执行 ----
static struct { unsigned long long ino; long long size; struct timespec mtime; int valid; } g_seen;
static uint64_t g_next_ns;
static int g_said_no_layer, g_said_no_set, g_said_no_screen;
static char g_applied[160];   // 上一轮实际生效的值（只在变化时打日志）

static uint64_t mono_ns(void) {
    struct timespec t; if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
static int same_as_seen(const struct stat *st) {
    return g_seen.valid && (unsigned long long)st->st_ino == g_seen.ino && (long long)st->st_size == g_seen.size &&
           st->st_mtimespec.tv_sec == g_seen.mtime.tv_sec && st->st_mtimespec.tv_nsec == g_seen.mtime.tv_nsec;
}
// 同一次读之前 / 之后是不是同一个文件版本（写入方没走 rename、写到一半被我们读到）
static int same_file(const struct stat *a, const struct stat *b) {
    return (unsigned long long)a->st_ino == (unsigned long long)b->st_ino && (long long)a->st_size == (long long)b->st_size &&
           a->st_mtimespec.tv_sec == b->st_mtimespec.tv_sec && a->st_mtimespec.tv_nsec == b->st_mtimespec.tv_nsec;
}
static void note(const struct stat *st) {
    g_seen.ino = (unsigned long long)st->st_ino; g_seen.size = (long long)st->st_size; g_seen.mtime = st->st_mtimespec; g_seen.valid = 1;
}
// refresh → 图形层的原子上限（graphics/nvn nvn_fps.c）；没有链接图形层（通用模式 / 单测）报一次红
static void apply_refresh(int v) {
    int (*set)(int, int) = (int (*)(int, int))dlsym(RTLD_DEFAULT, "nvn_fps_set");
    if (!set) { if (!g_said_no_set) { g_said_no_set = 1; LOG("【黄】运行中改帧率上限：本宿主没链直连图形层，忽略 refresh"); } return; }
    // 只在报得出屏幕最高刷新率的宿主（Mac 窗口壳 front_mac.m）改：iOS 走 Settings.bundle 的 0/30/60/120 + 重启生效，
    //   这里没有屏幕上限可夹，refresh=1000 会原样进显示链接（PR #38 复审阻塞 2）
    int (*scr)(void) = (int (*)(void))dlsym(RTLD_DEFAULT, "front_screen_max_fps");
    if (!scr || scr() <= 0) { if (!g_said_no_screen) { g_said_no_screen = 1; LOG("【黄】运行中改帧率上限：本宿主报不出屏幕刷新率（只有 Mac 支持），忽略 refresh"); } return; }
    int mode = v < 0 ? 1 : v == 0 ? 0 : 2;   // 1 = display、0 = off、2 = Hz（nvn_fps.h NVN_FPS_*）
    if (set(mode, v > 0 ? v : 0)) LOG("【红】运行中改帧率上限 %d 被拒绝（越界），保留原值", v);
}
// out → 环境变量（Mac 窗口壳在 -layout 里读），再让壳重摆一次；没有壳（无窗口局 / 单测）报一次黄
static void apply_out(const s2a_display_cmd *c) {
    char v[32];
    if (c->out_kind == 1) snprintf(v, sizeof v, "native");
    else if (c->out_kind == 2) snprintf(v, sizeof v, "fit");
    else snprintf(v, sizeof v, "%dx%d", c->out_w, c->out_h);
    setenv("S2A_OUT_SIZE", v, 1);
    void (*relayout)(void) = (void (*)(void))dlsym(RTLD_DEFAULT, "s2a_mac_relayout");
    if (relayout) relayout();
    else if (!g_said_no_layer) { g_said_no_layer = 1; LOG("【黄】运行中改输出栅格：本宿主没有可重摆的窗口壳，下一次布局时生效"); }
}
// 单测钩子：清掉限频与「读过的文件」记忆，让下一拍真的去读
void s2a_display_ctl_reset_for_test(void) { g_next_ns = 0; g_seen.valid = 0; g_applied[0] = 0; g_said_no_layer = g_said_no_set = g_said_no_screen = 0; }
void s2a_display_ctl_tick(void) {
    uint64_t now = mono_ns();
    if (g_next_ns && now < g_next_ns) return;
    g_next_ns = now + 500000000ull;   // 500 ms 一拍：帧路径上只有这一次比较
    char path[1024];
    if (s2a_docs_path(path, sizeof path, "display.txt") >= (int)sizeof path) return;
    struct stat st;
    if (stat(path, &st)) { g_seen.valid = 0; return; }   // 文件不在 = 没有运行中改
    if (same_as_seen(&st)) return;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    char text[S2A_DISPLAY_MAX];
    ssize_t got = read(fd, text, sizeof text - 1);
    close(fd);
    if (got < 0) return;
    text[got] = 0;
    struct stat st2;
    if (stat(path, &st2) || !same_file(&st, &st2)) return;   // 读到一半被换掉（写入方没走 rename）：整轮作废，下一拍再来
    note(&st2);
    s2a_display_cmd c; int bad_line = 0; const char *bad_why = NULL;
    int n = s2a_display_parse(text, &c, &bad_line, &bad_why);
    if (bad_line) LOG("【黄】display.txt 第 %d 行不认（%s），该键保留上一次的值", bad_line, bad_why ? bad_why : "格式");
    if (!n) return;
    char sum[160];
    snprintf(sum, sizeof sum, "refresh=%d out=%d:%dx%d fps60=%d", c.refresh, c.out_kind, c.out_w, c.out_h, c.fps60);
    if (strcmp(sum, g_applied)) { snprintf(g_applied, sizeof g_applied, "%s", sum); LOG("显示：%s（display.txt）", sum); }
    if (c.refresh != -2) apply_refresh(c.refresh);
    if (c.out_kind) apply_out(&c);
    if (c.fps60 >= 0) s2a_fps_set_enabled(c.fps60);
}
