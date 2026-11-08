// 包内显示默认值（出包阶段写的 <模块目录>/s2a_display.tsv，pack/stage_caps.sh 出）：
//   plat<TAB>macos
//   refresh<TAB>144|off|display<TAB># 出包时选的刷新率上限
//   out_size<TAB>2560x1440|native|fit<TAB># 出包时选的输出分辨率
// 只在对应环境变量未设时写（启动环境 > 玩家设置 > 本文件 > 宿主缺省），与包内能力清单同一优先级口径（runtime/src/caps.c）。
// 值链与运行中改（runtime/src/display_ctl.c 的 display.txt）是两回事：这里是包自带的默认值，那里是玩家局内改。
#pragma once

// 读 <dir>/s2a_display.tsv 并设 S2A_FPS / S2A_OUT_SIZE（未设才写）。文件不在 = 什么都不做。
//   plat 不是 macos 的清单不生效（防把别的平台的文件打进这棵树）。返回生效的行数；坏行打黄跳过，不猜。
int s2a_display_cfg_apply(const char *dir);
