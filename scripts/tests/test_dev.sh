#!/usr/bin/env bash
# Tests for ./dev argument handling.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
failures=0

check() {
  local name="$1"
  shift
  if "$@"; then
    printf 'ok   %s\n' "$name"
  else
    printf 'FAIL %s\n' "$name"
    failures=$((failures + 1))
  fi
}

help_lists_commands() {
  local out
  out="$("$root/dev" help)"
  [[ "$out" == *"build"* && "$out" == *"test-engine"* && "$out" == *"docs"* ]]
}

unknown_command_exits_2() {
  local rc=0
  "$root/dev" no-such-command >/dev/null 2>&1 || rc=$?
  [[ "$rc" -eq 2 ]]
}

no_argument_prints_help() {
  [[ "$("$root/dev")" == "$("$root/dev" help)" ]]
}

check "help lists the commands" help_lists_commands
check "an unknown command exits 2" unknown_command_exits_2
check "no argument prints help" no_argument_prints_help

[[ "$failures" -eq 0 ]]
