// 运行中改显示参数（docs/画面设置.md「运行中改」）：<S2A_DOCS>/display.txt，一行一个 key=value。
//   解析是纯函数（单测 runtime/tests/t_display_ctl.c），执行只在帧线程（ap_host.c s2a_frame_presented → s2a_display_ctl_tick）。
//   与包内默认值（pack 阶段写的 s2a_display.tsv）不是同一个文件：这里是玩家 / 启动器在局内改，值链仍是
//   启动环境 > 玩家设置 > 包内默认；文件不在 = 没有运行中改，行为与没有这个功能时完全一样。
#pragma once
#include <stddef.h>

#define S2A_DISPLAY_MAX 4096

// refresh：-2 = 文件没说；-1 = display（跟显示器）；0 = off（不限）；>0 = 该帧率（24–1000）
// out_kind：0 = 没说；1 = native；2 = fit；3 = WxH（out_w × out_h）
// fps60：-1 = 没说；0 / 1 = 关 / 开（runtime/src/fps.c 的 60 帧表开关）
typedef struct {
    int refresh, out_kind, out_w, out_h, fps60;
} s2a_display_cmd;

// 解析一段文本。返回认出的键数；第一处坏行的行号写进 *bad_line（0 = 没有坏行），原因写进 *bad_why（静态串，可为 NULL）。
//   注释（# 到行尾）、空行、CRLF、行首行尾空白都允许；键名与值都按整串认，尾部多一个字符就算坏行。
int s2a_display_parse(const char *text, s2a_display_cmd *c, int *bad_line, const char **bad_why);

// 每上屏一帧调一次；内部按 500 ms 限频，文件没变（inode / 大小 / 修改时间都相同）不重读。
//   写入方应写 display.txt.tmp 再 rename；读侧前后各 stat 一次，中途变了整轮作废，下一拍再来。
void s2a_display_ctl_tick(void);

// 单测钩子：清掉限频与「读过的文件」记忆（生产代码不调）
void s2a_display_ctl_reset_for_test(void);
