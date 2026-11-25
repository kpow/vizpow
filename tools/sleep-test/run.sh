#!/usr/bin/env bash
# Host tests for the shared sleep schedule (docs/plan-sleep.md).
# 1. The vizbot and vizmini copies of sleep_schedule.h must be byte-identical.
# 2. The tests build warning-free under -Werror and run clean under ASan + UBSan.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

a="$root/vizbot/sleep_schedule.h"
b="$root/vizmini/sleep_schedule.h"
if [[ -f "$b" ]] && ! cmp -s "$a" "$b"; then
  echo "FAIL: vizbot/sleep_schedule.h and vizmini/sleep_schedule.h differ" >&2
  exit 1
fi

c++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    "$here/test_sleep_schedule.cpp" -o "$out/t"
"$out/t"
