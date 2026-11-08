// 帧率上限的取值与运行中改（graphics/nvn/gfx/nvn/nvn_fps.c，直连三后端与浮层共用的那一份）。
// 期望值全部手算：环境只读一次（起局），所以每个取值单独起一个子进程；运行中只认 nvn_fps_set。
//   语法：null / 空 / 0 / off = 不限；display = 跟随显示器（替身里是 144 Hz）；24–1000 = 该帧率；其它一律不认（起局退出，运行中拒绝并保留旧值）。
//   上限只把周期拉长：period = max(原周期, 1/上限)，所以 60 帧的周期在 144 上限下原样、0 周期变成 1/144。
#include "../gfx/nvn/nvn_fps.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static char g_exe[4096];   // 自身可执行文件（子进程按环境重跑一遍：环境只在起局读一次）

int nvn_be_display_hz(void) { return 144; }   // 替身：144 Hz 屏（真实现：nvn_mtl.m nvn_be_display_hz，无窗口返回 0）

static int g_bad, g_n;
#define CHECK(c, ...) do { g_n++; if (!(c)) { g_bad++; printf("✗ "); printf(__VA_ARGS__); printf("\n"); } } while (0)

// 子进程：按环境起局那一次，把解析结果打成一行
static int env_self(const char *value) {
    if (value) setenv("S2A_FPS", value, 1);
    printf("mode=%d limit=%d req=%d period0=%.9f\n", nvn_fps_mode(), nvn_fps_limit(), nvn_fps_requested(), nvn_fps_period(0));
    return 0;
}
// 父进程：起一个子进程读它的那一行；abort 的子进程返回 -1
static int env_probe(const char *value, int *mode, int *limit, int *req, double *p0) {
    int fd[2];
    if (pipe(fd)) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        close(fd[0]); dup2(fd[1], STDOUT_FILENO); close(fd[1]);
        char *args[4] = {"t_nvn_fps", "--env", (char *)value, NULL};   // 参数：--env [值]
        if (!value) args[2] = NULL;
        execv(g_exe, args);
        _exit(97);
    }
    close(fd[1]);
    char buf[256] = {0};
    ssize_t n = read(fd[0], buf, sizeof buf - 1);
    close(fd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    if (n <= 0) return -1;   // abort / 没有输出
    return sscanf(buf, "mode=%d limit=%d req=%d period0=%lf", mode, limit, req, p0) == 4 ? 0 : -1;
}
static void env_case(const char *name, const char *value, int em, int el, int er, double ep) {
    int m = -9, l = -9, r = -9; double p = -9;
    int rc = env_probe(value, &m, &l, &r, &p);
    CHECK(rc == 0 && m == em && l == el && r == er && fabs(p - ep) < 1e-9,
          "%s：起局 S2A_FPS=%s 期望 模式%d 上限%d 请求%d 周期0 %.9f 实得 返回%d 模式%d 上限%d 请求%d 周期0 %.9f",
          name, value ? value : "（未设）", em, el, er, ep, rc, m, l, r, p);
}
static void env_bad(const char *value) {
    int m, l, r; double p;
    CHECK(env_probe(value, &m, &l, &r, &p) == -1, "坏值 S2A_FPS=%s：起局必须退出", value);
}

int main(int argc, char **argv) {
    if (!realpath(argv[0], g_exe)) snprintf(g_exe, sizeof g_exe, "%s", argv[0]);
    if (argc >= 2 && !strcmp(argv[1], "--env")) return env_self(argc >= 3 ? argv[2] : NULL);

    // ① 起局读环境（每值一进程）
    env_case("未设 = 不限", NULL, NVN_FPS_OFF, 0, 0, 0.0);
    env_case("0 = 不限", "0", NVN_FPS_OFF, 0, 0, 0.0);
    env_case("off = 不限", "off", NVN_FPS_OFF, 0, 0, 0.0);
    env_case("display = 跟屏", "display", NVN_FPS_DISPLAY, 144, -1, 1.0 / 144);
    env_case("60", "60", NVN_FPS_HZ, 60, 60, 1.0 / 60);
    env_case("120", "120", NVN_FPS_HZ, 120, 120, 1.0 / 120);
    env_case("144", "144", NVN_FPS_HZ, 144, 144, 1.0 / 144);
    env_case("165", "165", NVN_FPS_HZ, 165, 165, 1.0 / 165);
    env_case("240", "240", NVN_FPS_HZ, 240, 240, 1.0 / 240);
    env_case("下界 24", "24", NVN_FPS_HZ, 24, 24, 1.0 / 24);
    env_case("上界 1000", "1000", NVN_FPS_HZ, 1000, 1000, 1.0 / 1000);
    env_bad("30abc");
    env_bad("12");
    env_bad("-1");
    env_bad("1001");
    env_bad("120display");

    // ② 运行中改（本进程没有起局环境，所以起始 = 不限）
    CHECK(nvn_fps_mode() == NVN_FPS_OFF && nvn_fps_limit() == 0, "运行中：起始不限");
    CHECK(nvn_fps_set(NVN_FPS_DISPLAY, 0) == 0 && nvn_fps_limit() == 144, "运行中：display → 144（替身屏）");
    CHECK(nvn_fps_requested() == -1, "运行中：display 的请求值是 -1");
    CHECK(nvn_fps_set(NVN_FPS_HZ, 144) == 0 && nvn_fps_limit() == 144, "运行中：144 生效");
    CHECK(fabs(nvn_fps_period(0) - 1.0 / 144) < 1e-9, "上限 144：0 周期变成 1/144");
    CHECK(fabs(nvn_fps_period(1.0 / 60) - 1.0 / 60) < 1e-9, "上限 144：60 帧周期原样（上限只拉长）");
    CHECK(nvn_fps_period_ns(0) == (1000000000ull + 143) / 144, "上限 144：0 周期的纳秒值");
    CHECK(nvn_fps_set(NVN_FPS_HZ, 12) == -1 && nvn_fps_limit() == 144, "运行中：12 拒绝，保留 144");
    CHECK(nvn_fps_set(NVN_FPS_HZ, 1001) == -1 && nvn_fps_limit() == 144, "运行中：1001 拒绝，保留 144");
    CHECK(nvn_fps_set(99, 60) == -1 && nvn_fps_limit() == 144, "运行中：不认的种类拒绝，保留 144");
    CHECK(nvn_fps_set(NVN_FPS_HZ, 60) == 0 && nvn_fps_limit() == 60, "运行中：60 生效");
    CHECK(nvn_fps_set(NVN_FPS_OFF, 0) == 0 && nvn_fps_limit() == 0, "运行中：关掉回不限");

    printf("%s t_nvn_fps：帧率上限取值与热切 %d 项 → %s\n", g_bad ? "✗" : "✓", g_n, g_bad ? "有不符合项" : "全过");
    return g_bad != 0;
}
