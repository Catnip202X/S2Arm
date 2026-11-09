// 低频链路档降档退避（graphics/nvn/gfx/nvn/nvn_link_backoff.h）：时间由用例给，期望值按规则手算，不跑 link_sync。
// ① 初值 1 秒 ② 进低频档 0.5 秒被打回 → 2 秒，再来回切 → 4、8、16、32、64 封顶 ③ 低频档待 2.5 秒才被打回（正常换档）→ 不翻倍、1.9 秒 → 翻倍
// ④ 低频档稳住 60 秒 → 恢复 1 秒；59.9 秒时不恢复 ⑤ 不在低频档时被「打回」（没有 since）不翻倍
// ⑥ 金刚形态逐帧推演：每 1.45 秒间隔 3 + 1 帧间隔 2，按 link_sync 的「稳 hold 秒才降档」规则，60 秒内降档次数 = 1（不退避时 ≈ 每 1.5 秒一次）
// ⑦ 高刷新档（5）不是低频档（PR #38 复审阻塞 1）：lb_exact 对 5 恒为能表示、lb_applied 不对 5 记时；逐帧推演不锁帧、上限 144 的局 10 秒留在 5、0 次打回、hold 不变；
//    负对照：原 link_exact（5 按 30 Hz 算）在 1/144 得 t = 0.208 → 判表示不了
#include <math.h>
#include <stdio.h>
#include "nvn_link_backoff.h"

static int n, bad;
#define CHECK(c, ...) do { n++; if (!(c)) { bad++; printf("没过 "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static const uint64_t S = LB_S;

int main(void) {
    link_backoff b = {0};
    CHECK(lb_hold(&b) == 1 * S, "① 初值 hold %llu", (unsigned long long)lb_hold(&b));
    // ②
    uint64_t t = 100 * S; lb_applied(&b, 1, 4, t); lb_forced_back(&b, t + S / 2); lb_applied(&b, 4, 1, t + S / 2);
    CHECK(lb_hold(&b) == 2 * S, "② 0.5 秒被打回 hold %llu，期望 2 秒", (unsigned long long)(lb_hold(&b) / S));
    uint64_t want[] = {4, 8, 16, 32, 64, 64};
    for (int i = 0; i < 6; i++) { t += 10 * S; lb_applied(&b, 1, 3, t); lb_forced_back(&b, t + S); lb_applied(&b, 3, 1, t + S);
        CHECK(lb_hold(&b) == want[i] * S, "② 第 %d 次来回切 hold %llu，期望 %llu", i + 2, (unsigned long long)(lb_hold(&b) / S), (unsigned long long)want[i]); }
    // ③ 正常换档不算来回切：低频档待 2.5 秒（单测动态阶段 0：间隔 2 稳 1 秒降 30 Hz、3.6 秒时转间隔 3）被打回 → 不翻倍；1.9 秒 → 翻倍
    link_backoff c = {0}; lb_applied(&c, 1, 3, 1 * S); lb_forced_back(&c, 1 * S + 2500000000ull);
    CHECK(lb_hold(&c) == 1 * S, "③ 2.5 秒后被打回 hold %llu，期望 1 秒", (unsigned long long)(lb_hold(&c) / S));
    link_backoff c2 = {0}; lb_applied(&c2, 1, 3, 1 * S); lb_forced_back(&c2, 1 * S + 1900000000ull);
    CHECK(lb_hold(&c2) == 2 * S, "③ 1.9 秒被打回 hold %llu，期望 2 秒", (unsigned long long)(lb_hold(&c2) / S));
    // ④
    link_backoff d = {0, 16 * S}; lb_applied(&d, 1, 3, 10 * S);
    lb_tick(&d, 10 * S + 59900000000ull); CHECK(lb_hold(&d) == 16 * S, "④ 59.9 秒不恢复 hold %llu", (unsigned long long)(lb_hold(&d) / S));
    lb_tick(&d, 70 * S); CHECK(lb_hold(&d) == 1 * S, "④ 60 秒恢复 hold %llu，期望 1 秒", (unsigned long long)(lb_hold(&d) / S));
    // ⑤
    link_backoff e = {0}; lb_forced_back(&e, 5 * S); CHECK(lb_hold(&e) == 1 * S, "⑤ 不在低频档 hold %llu", (unsigned long long)(lb_hold(&e) / S));
    // ⑥ 逐帧推演：周期 50 ms（间隔 3）29 帧 + 33.3 ms（间隔 2）1 帧；降档条件 = 同一周期已稳 hold；低频档 20 Hz 遇 33.3 ms 立即回基础档
    link_backoff f = {0}; int mode = 1, drops = 0; uint64_t now = 0, stable_since = 0, prev = 0;
    for (int fr = 0; now < 60 * S; fr++) {
        uint64_t period = fr % 30 == 29 ? 33333333ull : 50000000ull;
        if (period != prev) { stable_since = now; prev = period; }
        lb_tick(&f, now);
        if (mode == 4 && period != 50000000ull) { lb_forced_back(&f, now); lb_applied(&f, 4, 1, now); mode = 1; }
        else if (mode == 1 && period == 50000000ull && now - stable_since >= lb_hold(&f)) { lb_applied(&f, 1, 4, now); mode = 4; drops++; }
        now += period;
    }
    CHECK(drops == 1, "⑥ 金刚形态 60 秒降档 %d 次，期望 1", drops);
    // ⑥ 反证：持续间隔 3 时第一秒后就降 20 Hz 且一直不被打回
    link_backoff g = {0}; int down_at = -1; now = 0;
    for (int fr = 0; fr < 100; fr++, now += 50000000ull) if (down_at < 0 && now >= lb_hold(&g)) { lb_applied(&g, 1, 4, now); down_at = fr; }
    CHECK(down_at == 20, "⑥ 持续间隔 3 第 %d 帧降 20 Hz，期望第 20 帧（1 秒）", down_at);
    // ⑦
    CHECK(lb_low(3) && lb_low(4) && !lb_low(1) && !lb_low(2) && !lb_low(5), "⑦ 低频档只有 3 / 4");
    CHECK(lb_exact(5, 1.0 / 144) && lb_exact(5, 1.0 / 1000) && lb_exact(5, 1.0 / 60) && lb_exact(5, 0.05), "⑦ 高刷新档任何周期都能表示");
    CHECK(lb_exact(3, 1.0 / 30) && lb_exact(3, 1.0 / 15) && !lb_exact(3, 0.05) && !lb_exact(3, 1.0 / 60), "⑦ 30 Hz 档判定不变（33.3 / 66.7 ms 能，50 / 16.7 ms 不能）");
    CHECK(lb_exact(4, 0.05) && !lb_exact(4, 1.0 / 30), "⑦ 20 Hz 档判定不变（50 ms 能，33.3 ms 不能）");
    { double t = (1.0 / 144) * 30; int old_exact = t >= 1 - 1e-6 && fabs(t - round(t)) < 1e-6; CHECK(!old_exact, "⑦ 负对照：原公式应判 1/144 在模式 5 表示不了"); }
    link_backoff h = {0}; lb_applied(&h, 1, 5, 3 * S); CHECK(h.since == 0, "⑦ 进高刷新档不记低频时刻 since=%llu", (unsigned long long)h.since);
    { int m = 1, back = 0; uint64_t tt = 0, p = 6944444ull, pend = 0;   // 1/144 s；进 5 前要同一周期稳 1 秒（link_sync 对非低频档用 LB_S）
      for (; tt < 10 * S; tt += p) {
          lb_tick(&h, tt);
          if (m == 5 && lb_low(m) && !lb_exact(m, p / 1e9)) { lb_forced_back(&h, tt); lb_applied(&h, 5, 1, tt); m = 1; back++; }
          else if (m == 1 && tt - pend >= (lb_low(5) ? lb_hold(&h) : LB_S)) { lb_applied(&h, 1, 5, tt); m = 5; }
      }
      CHECK(m == 5 && back == 0 && lb_hold(&h) == 1 * S, "⑦ 不锁帧上限 144：10 秒后模式 %d、打回 %d 次、hold %llu 秒（期望 5、0、1）", m, back, (unsigned long long)(lb_hold(&h) / S)); }
    printf("%s t_link_backoff：%d 项，没过 %d\n", bad ? "✗" : "✓", n, bad);
    return bad != 0;
}
