#!/usr/bin/env bash

set -euo pipefail

repo_root() {
  git rev-parse --show-toplevel
}

ensure_on_repo_root() {
  cd "$(repo_root)"
}

next_snapshot_index() {
  local dir
  dir="$(repo_root)/进度快照"

  if ! find "$dir" -maxdepth 1 -type f -name '*.md' | grep -q .; then
    echo "001"
    return
  fi

  find "$dir" -maxdepth 1 -type f -name '*.md' -printf '%f\n' \
    | sed -E 's/^([0-9]+).*/\1/' \
    | sort -n \
    | tail -n 1 \
    | awk '{ printf "%03d\n", $1 + 1 }'
}

next_version_tag() {
  local dir latest
  dir="$(repo_root)/dist"

  latest="$(find "$dir" -maxdepth 1 -mindepth 1 -type d -printf '%f\n' \
    | sed -nE 's/^v([0-9]+)\.([0-9]+)\.([0-9]+)$/\1 \2 \3/p' \
    | sort -n -k1,1 -k2,2 -k3,3 \
    | tail -n 1 || true)"

  if [[ -z "$latest" ]]; then
    echo "v0.1.0"
    return
  fi

  awk '{ printf "v%d.%d.%d\n", $1, $2, $3 + 1 }' <<<"$latest"
}

next_backup_version() {
  local dir latest
  dir="$(repo_root)/backup"

  latest="$(find "$dir" -maxdepth 1 -mindepth 1 -type d -printf '%f\n' \
    | sed -nE 's/^v([0-9]+)\.([0-9]+)\.([0-9]+)$/\1 \2 \3/p' \
    | sort -n -k1,1 -k2,2 -k3,3 \
    | tail -n 1 || true)"

  if [[ -z "$latest" ]]; then
    echo "v0.1.0"
    return
  fi

  awk '{ printf "v%d.%d.%d\n", $1, $2, $3 + 1 }' <<<"$latest"
}
