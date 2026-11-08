# Android 设计/状态文档（自开发线同步）

这批文档来自开发线（原路径 `platforms/android/docs/`），本树对应目录是 `android/`。
因此正文里出现的 `platforms/android/…` 是**开发线路径**，在本树请读作 `android/…`；
相对链接与命令示例已按本树布局改写，少数表格/说明里保留原名以对应上游记录。

入口按用途：

| 想知道 | 看 |
| --- | --- |
| 目标、架构、着色器、交付顺序、验收标准 | `fast-generic-v3.md` |
| 当前状态、C0–C8 证据入口、开放工作 | `STATUS.md` |
| 里程碑达成/回滚/验收结论变化 | `CHANGELOG.md`、`RUNLOG.md` |
| 真机证据（安装/升级/资源包/身份/输入） | `device-matrix.md` |
| 直连（direct）模式契约与历史证据 | `direct-mode-contracts.md`、`direct-mode-history-20261026-28.md`、`direct-main-merge-analysis-20261028.md` |
| GPU 能力与优化、着色器缓存方案 | `gpu-compatibility.md`、`shader-plan.md` |
| 实施方案与合并分析 | `implementation-plan.md` |
| 自动化跑一局 | `autonomous-game-run.md` |
| 设备/输入法验证 | `c6-ime-device-validation.md`、`fps-overlay.md` |

判据口径以 `STATUS.md` §3 为准：只有真机跑出来的才算证据；合成包能覆盖的标注「合成」，需要真实游戏的标注「需真游戏」。
