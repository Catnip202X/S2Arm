#!/usr/bin/env bash
# 门禁：用户会照抄的文本里，不许把远程脚本直接管道喂给 shell。
#
# 判据（红 = 有命中）：在 .sh/.py/.md/.cs/.swift/.m 里出现 `curl … | bash`、
# `curl … | sh`、`wget … | bash` 形态的**一行命令**。管道把「下载」和「执行」
# 合成一步，用户与评审都看不到被执行的字节；本仓库在 pack/jobs/toolchain.py
# 里对同一个安装脚本就是「先落盘再执行」，文本应与实现同口径。
#
# 射程：全树，排除 docs/（外部链接与规范引用）、compat/tree/（vendored 上游）、
# 以及本文件自身（判据里的模式字面量）。命中不区分「文档/脚本/代码」——用户
# 复制的正是这些字符串。
#
# 用法：bash tests/no_remote_exec_pipe_test.sh [仓库根]；非 0 退出 = 有命中。
set -u
ROOT=${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
cd "$ROOT" || exit 2

PAT='(curl|wget)[^|]*\|[[:space:]]*(sudo[[:space:]]+)?(ba|z|k)?sh'
SELF_REL="tests/no_remote_exec_pipe_test.sh"

HITS=$(grep -rnE "$PAT" \
        --include=*.sh --include=*.py --include=*.md \
        --include=*.cs --include=*.swift --include=*.m \
        . 2>/dev/null \
      | grep -v '^\./docs/' \
      | grep -v '^\./compat/tree/' \
      | grep -v "^\./$SELF_REL:" \
      | grep -v '^\./\.git/')

if [ -n "$HITS" ]; then
  n=$(printf '%s\n' "$HITS" | grep -c .)
  echo "🔴 有 $n 处把远程脚本管道喂给 shell（应落盘、看哈希、再执行）："
  printf '%s\n' "$HITS" | sed 's/^/    /'
  exit 1
fi
echo "✅ 没有「远程脚本 | shell」形态的命令"
exit 0
