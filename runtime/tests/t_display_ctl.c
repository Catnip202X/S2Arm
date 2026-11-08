// 运行中改显示参数（runtime/src/display_ctl.c）：解析是纯函数，执行侧只用替身观察调用。
// 期望值全部手写：refresh 三种形态 + 越界、out 三种形态 + 越界、fps60 两值 + 越界、注释/空行/CRLF/未知键、坏行只报第一处。
// 执行侧：S2A_DOCS 指到临时目录，写 display.txt → tick 一次 → 核 S2A_OUT_SIZE 与替身收到的 nvn_fps_set / s2a_fps_set_enabled。
#include "../include/s2a_display_ctl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_bad, g_n;
#define CHECK(c, ...) do { g_n++; if (!(c)) { g_bad++; printf("✗ "); printf(__VA_ARGS__); printf("\n"); } } while (0)

// 替身：图形层上限与 60 帧开关（display_ctl 经 dlsym(RTLD_DEFAULT) 取，链接要 -Wl,-export_dynamic）
static int g_mode = -9, g_hz = -9, g_set_n, g_fps60 = -9;
int nvn_fps_set(int mode, int hz) { g_mode = mode; g_hz = hz; g_set_n++; return 0; }
void s2a_fps_set_enabled(int on) { g_fps60 = on; }

static void parse_case(const char *name, const char *text, int er, int eok, int ew, int eh, int e60, int ebad) {
    s2a_display_cmd c; int bad = 0; const char *why = NULL;
    int n = s2a_display_parse(text, &c, &bad, &why);
    CHECK(c.refresh == er && c.out_kind == eok && c.out_w == ew && c.out_h == eh && c.fps60 == e60 && bad == ebad,
          "%s：期望 refresh=%d out_kind=%d %dx%d fps60=%d 坏行%d；实得 refresh=%d out_kind=%d %dx%d fps60=%d 坏行%d（%s）",
          name, er, eok, ew, eh, e60, ebad, c.refresh, c.out_kind, c.out_w, c.out_h, c.fps60, bad, why ? why : "无");
    (void)n;
}
static void write_file(const char *dir, const char *text) {
    char p[1024]; snprintf(p, sizeof p, "%s/display.txt", dir);
    FILE *f = fopen(p, "wb"); if (!f) { printf("✗ 写不了 %s\n", p); g_bad++; return; }
    fwrite(text, 1, strlen(text), f); fclose(f);
}

int main(void) {
    // ① 解析
    parse_case("空文本", "", -2, 0, 0, 0, -1, 0);
    parse_case("只有注释与空行", "# 注释\n\n   \n", -2, 0, 0, 0, -1, 0);
    parse_case("refresh=144", "refresh=144\n", 144, 0, 0, 0, -1, 0);
    parse_case("refresh=display", "refresh=display", -1, 0, 0, 0, -1, 0);
    parse_case("refresh=off", "refresh=off", 0, 0, 0, 0, -1, 0);
    parse_case("refresh 越界 12", "refresh=12\n", -2, 0, 0, 0, -1, 1);
    parse_case("refresh 越界 1001", "refresh=1001\n", -2, 0, 0, 0, -1, 1);
    parse_case("refresh 尾部杂字符", "refresh=60x\n", -2, 0, 0, 0, -1, 1);
    parse_case("out=2560x1440", "out=2560x1440\n", -2, 3, 2560, 1440, -1, 0);
    parse_case("out=native", "out=native\n", -2, 1, 0, 0, -1, 0);
    parse_case("out=fit", "out=fit\n", -2, 2, 0, 0, -1, 0);
    parse_case("out 越界 20000x1440", "out=20000x1440\n", -2, 0, 0, 0, -1, 1);
    parse_case("out 不是尺寸", "out=big\n", -2, 0, 0, 0, -1, 1);
    parse_case("fps60=0", "fps60=0\n", -2, 0, 0, 0, 0, 0);
    parse_case("fps60=1", "fps60=1\n", -2, 0, 0, 0, 1, 0);
    parse_case("fps60=2 不认", "fps60=2\n", -2, 0, 0, 0, -1, 1);
    parse_case("CRLF + 行首尾空白", "  refresh = 60 \r\n", 60, 0, 0, 0, -1, 0);
    parse_case("行尾注释", "out=1920x1080  # 1080p\n", -2, 3, 1920, 1080, -1, 0);
    parse_case("未知键忽略", "future=1\nrefresh=120\n", 120, 0, 0, 0, -1, 0);
    parse_case("坏行只报第一处", "refresh=12\nout=big\n", -2, 0, 0, 0, -1, 1);
    parse_case("没有等号", "refresh 144\n", -2, 0, 0, 0, -1, 1);
    parse_case("三键同文件", "refresh=display\nout=2560x1440\nfps60=1\n", -1, 3, 2560, 1440, 1, 0);

    // ② 执行：文件 → tick → 替身
    char dir[] = "/tmp/t_display_ctl.XXXXXX";
    if (!mkdtemp(dir)) { printf("✗ 建不了临时目录\n"); return 1; }
    setenv("S2A_DOCS", dir, 1);
    unsetenv("S2A_OUT_SIZE");
    write_file(dir, "refresh=144\nout=1920x1080\nfps60=1\n");
    s2a_display_ctl_reset_for_test(); g_set_n = 0;
    s2a_display_ctl_tick();
    CHECK(g_set_n == 1 && g_mode == 2 && g_hz == 144, "执行：refresh=144 → nvn_fps_set(2,144)，实得 调用%d 次 模式%d %d Hz", g_set_n, g_mode, g_hz);
    const char *o = getenv("S2A_OUT_SIZE");
    CHECK(o && !strcmp(o, "1920x1080"), "执行：out=1920x1080 → S2A_OUT_SIZE，实得 %s", o ? o : "（未设）");
    CHECK(g_fps60 == 1, "执行：fps60=1 → s2a_fps_set_enabled(1)，实得 %d", g_fps60);

    // 文件没变：不重读、不重复执行（限频记忆 + 同一 inode / 大小 / 时间）
    s2a_display_ctl_reset_for_test();
    g_set_n = 0; g_fps60 = -9;
    s2a_display_ctl_tick();          // 第一次读，记下状态
    s2a_display_ctl_reset_for_test(); g_set_n = 0; g_fps60 = -9;
    s2a_display_ctl_tick();          // 文件没变（reset 后仍要重读一次）；这里只核「不改文件就不会变值」
    CHECK(g_mode == 2 && g_hz == 144, "执行：文件没变时上限仍是 144（实得 %d / %d Hz）", g_mode, g_hz);

    // 改成新值：下一拍生效
    write_file(dir, "refresh=off\nout=native\nfps60=0\n");
    s2a_display_ctl_reset_for_test(); g_set_n = 0; g_fps60 = -9;
    s2a_display_ctl_tick();
    CHECK(g_set_n == 1 && g_mode == 0 && g_hz == 0, "执行：refresh=off → nvn_fps_set(0,0)，实得 调用%d 次 模式%d %d", g_set_n, g_mode, g_hz);
    o = getenv("S2A_OUT_SIZE");
    CHECK(o && !strcmp(o, "native"), "执行：out=native → S2A_OUT_SIZE，实得 %s", o ? o : "（未设）");
    CHECK(g_fps60 == 0, "执行：fps60=0 → s2a_fps_set_enabled(0)，实得 %d", g_fps60);

    // 坏行：不执行、不改值（保留上一次）
    write_file(dir, "refresh=12\n");
    s2a_display_ctl_reset_for_test(); g_set_n = 0;
    s2a_display_ctl_tick();
    CHECK(g_set_n == 0, "坏行：refresh=12 不调用 nvn_fps_set，实得调用 %d 次", g_set_n);

    // 没有文件：什么都不做
    { char gone[1024]; snprintf(gone, sizeof gone, "%s/display.txt", dir); unlink(gone); }
    s2a_display_ctl_reset_for_test(); g_set_n = 0;
    s2a_display_ctl_tick();
    CHECK(g_set_n == 0, "没有文件：不调用，实得 %d 次", g_set_n);

    printf("%s t_display_ctl：display.txt 解析与执行 %d 项 → %s\n", g_bad ? "✗" : "✓", g_n, g_bad ? "有不符合项" : "全过");
    return g_bad != 0;
}
