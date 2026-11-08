// 包内显示默认值（runtime/src/display_cfg.c）：读 <模块目录>/s2a_display.tsv，只在环境变量未设时写。
// 期望手写：macos 清单两行都生效；启动环境已给的键不覆盖（日志写明）；plat 不是 macos 整份不生效；
//   坏值跳过不影响另一行；文件不在 = 一行都不设。
#include "../include/s2a_display_cfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_bad, g_n;
#define CHECK(c, ...) do { g_n++; if (!(c)) { g_bad++; printf("✗ "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void write_file(const char *dir, const char *text) {
    char p[1024]; snprintf(p, sizeof p, "%s/s2a_display.tsv", dir);
    FILE *f = fopen(p, "wb"); if (!f) { printf("✗ 写不了 %s\n", p); g_bad++; return; }
    fwrite(text, 1, strlen(text), f); fclose(f);
}
static void clear_env(void) { unsetenv("S2A_FPS"); unsetenv("S2A_OUT_SIZE"); }
static int is(const char *k, const char *want) { const char *v = getenv(k); return v && !strcmp(v, want); }

int main(void) {
    char dir[] = "/tmp/t_display_cfg_XXXXXX";   // mkdtemp 要求模板以 6 个 X 结尾（带后缀会退化成字面量、第二次跑就 EEXIST）
    if (!mkdtemp(dir)) { printf("✗ 建不了临时目录\n"); return 1; }

    // ① 文件不在：一行都不设
    clear_env();
    CHECK(s2a_display_cfg_apply(dir) == 0 && !getenv("S2A_FPS") && !getenv("S2A_OUT_SIZE"), "文件不在：什么都不设");

    // ② macos 清单两行都生效
    write_file(dir, "plat\tmacos\n# 注释\nrefresh\t144\t# 刷新率上限\nout_size\t2560x1440\t# 输出分辨率\n");
    clear_env();
    CHECK(s2a_display_cfg_apply(dir) == 2, "macos 清单：生效两行");
    CHECK(is("S2A_FPS", "144"), "macos 清单：S2A_FPS=144，实得 %s", getenv("S2A_FPS") ? getenv("S2A_FPS") : "（未设）");
    CHECK(is("S2A_OUT_SIZE", "2560x1440"), "macos 清单：S2A_OUT_SIZE=2560x1440，实得 %s", getenv("S2A_OUT_SIZE") ? getenv("S2A_OUT_SIZE") : "（未设）");

    // ③ 启动环境已给：不覆盖（优先级：启动环境 > 玩家设置 > 包内默认）
    clear_env(); setenv("S2A_FPS", "display", 1);
    CHECK(s2a_display_cfg_apply(dir) == 1 && is("S2A_FPS", "display"), "启动环境给了 S2A_FPS=display：不覆盖（实得 %s）", getenv("S2A_FPS"));
    CHECK(is("S2A_OUT_SIZE", "2560x1440"), "启动环境没给 out：仍按包内默认写");

    // ④ plat 不是 macos：整份不生效
    write_file(dir, "plat\tandroid\nrefresh\t120\nout_size\t1920x1080\n");
    clear_env();
    CHECK(s2a_display_cfg_apply(dir) == 0 && !getenv("S2A_FPS") && !getenv("S2A_OUT_SIZE"), "plat android：整份不生效");

    // ⑤ 坏值跳过，另一行照常
    write_file(dir, "plat\tmacos\nrefresh\t12\nout_size\t1920x1080\n");
    clear_env();
    CHECK(s2a_display_cfg_apply(dir) == 1 && !getenv("S2A_FPS"), "refresh=12 越界：跳过，不设 S2A_FPS");
    CHECK(is("S2A_OUT_SIZE", "1920x1080"), "out_size 仍生效");

    // ⑥ 三种取值形态都认：off / display / 尺寸越界
    write_file(dir, "plat\tmacos\nrefresh\toff\nout_size\t20000x1440\n");
    clear_env();
    CHECK(s2a_display_cfg_apply(dir) == 1 && is("S2A_FPS", "off"), "refresh=off 生效，out_size 20000 越界跳过");

    clear_env();
    CHECK(s2a_display_cfg_apply(NULL) == 0 && s2a_display_cfg_apply("") == 0, "空目录参数：不做事");

    printf("%s t_display_cfg：包内显示默认值 %d 项 → %s\n", g_bad ? "✗" : "✓", g_n, g_bad ? "有不符合项" : "全过");
    return g_bad != 0;
}
