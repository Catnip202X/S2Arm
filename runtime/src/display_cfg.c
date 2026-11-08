// 包内显示默认值：<模块目录>/s2a_display.tsv（pack/stage_caps.sh 出，口径见 runtime/include/s2a_display_cfg.h）。
//   起局装载前读一次（host_main.c 与 s2a_caps_apply 同处），只在环境变量未设时写，所以玩家设置与启动环境都优先。
#include "native.h"
#include "../include/s2a_display_cfg.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    return s;
}
static int refresh_ok(const char *v) {
    if (!strcmp(v, "off") || !strcmp(v, "display")) return 1;
    char tail = 0; int hz = 0;
    if (sscanf(v, "%d%c", &hz, &tail) != 1) return 0;
    return hz >= 24 && hz <= 1000;
}
static int size_ok(const char *v) {
    if (!strcmp(v, "native") || !strcmp(v, "fit")) return 1;
    int a = 0, b = 0; char tail = 0;
    if (sscanf(v, "%dx%d%c", &a, &b, &tail) != 2) return 0;
    return a >= 1 && b >= 1 && a <= 16384 && b <= 16384;
}
int s2a_display_cfg_apply(const char *dir) {
    if (!dir || !*dir) return 0;
    char path[1200];
    int n = snprintf(path, sizeof path, "%s/s2a_display.tsv", dir);
    if (n < 0 || n >= (int)sizeof path) { LOG("【黄】包内显示默认值路径太长，忽略"); return 0; }
    FILE *f = fopen(path, "r");
    if (!f) return 0;   // 没这份文件 = 这个包没带显示默认值，行为与没有这个功能时一样
    char line[512], plat[32] = "";
    int applied = 0, lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        const char *s = trim(line);
        if (!*s) continue;
        char *tab = strchr((char *)s, '\t');
        if (!tab) { LOG("【黄】s2a_display.tsv 第 %d 行没有制表符，跳过", lineno); continue; }
        *tab = 0;
        const char *k = trim((char *)s), *v = trim(tab + 1);
        if (!strcmp(k, "plat")) { snprintf(plat, sizeof plat, "%s", v); continue; }
        if (*plat && strcmp(plat, "macos")) { fclose(f); LOG("【黄】s2a_display.tsv 的平台是 %s，不是 macos，整份不生效", plat); return 0; }
        if (!strcmp(k, "refresh")) {
            if (!refresh_ok(v)) { LOG("【黄】s2a_display.tsv refresh=%s 不认（off / display / 24–1000），跳过", v); continue; }
            if (getenv("S2A_FPS")) { LOG("包内显示默认值：refresh=%s（启动环境 / 玩家设置已给 S2A_FPS，不覆盖）", v); continue; }
            if (setenv("S2A_FPS", v, 0)) { LOG("【红】包内显示默认值 refresh 设不进去（errno %d）", errno); continue; }
            LOG("包内显示默认值：S2A_FPS=%s（出包时选的刷新率上限）", v); applied++;
        } else if (!strcmp(k, "out_size")) {
            if (!size_ok(v)) { LOG("【黄】s2a_display.tsv out_size=%s 不认（native / fit / WxH），跳过", v); continue; }
            if (getenv("S2A_OUT_SIZE")) { LOG("包内显示默认值：out_size=%s（启动环境 / 玩家设置已给 S2A_OUT_SIZE，不覆盖）", v); continue; }
            if (setenv("S2A_OUT_SIZE", v, 0)) { LOG("【红】包内显示默认值 out_size 设不进去（errno %d）", errno); continue; }
            LOG("包内显示默认值：S2A_OUT_SIZE=%s（出包时选的输出分辨率）", v); applied++;
        } else LOG("【黄】s2a_display.tsv 第 %d 行键 %s 不认，跳过", lineno, k);
    }
    fclose(f);
    return applied;
}
