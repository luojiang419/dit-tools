#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

ensure_on_repo_root

version="$(next_backup_version)"
target="$(repo_root)/backup/$version"

mkdir -p "$target"

if [[ -d dit-tools-src ]]; then
  cp -a dit-tools-src "$target/"
fi

if [[ -d tool ]]; then
  cp -a tool "$target/"
fi

if [[ -d docs ]]; then
  cp -a docs "$target/"
fi

if [[ -d installer ]]; then
  cp -a installer "$target/"
fi

if [[ -d services ]]; then
  cp -a services "$target/"
fi

if [[ -f .gitignore ]]; then
  cp .gitignore "$target/"
fi

echo "$target"
