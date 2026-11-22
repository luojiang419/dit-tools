#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

if [[ $# -lt 5 ]]; then
  echo "用法: $0 \"简要描述\" \"已完成内容\" \"当前模块\" \"待办清单\" \"下一步\"" >&2
  exit 1
fi

ensure_on_repo_root

index="$(next_snapshot_index)"
title="$1"
completed="$2"
module="$3"
todo="$4"
next_step="$5"
file="$(repo_root)/进度快照/${index}-${title}.md"

cat >"$file" <<EOF
# 进度快照 ${index}

- 时间：$(date '+%Y-%m-%d %H:%M:%S')
- 已完成内容：${completed}
- 当前修改模块：${module}
- 待办清单：${todo}
- 下一步：${next_step}
EOF

echo "$file"
