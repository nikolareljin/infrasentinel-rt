#!/usr/bin/env bash
# Logging helpers. Source this file; it defines functions only.

log_info() { printf '[isrt] %s\n' "$*"; }
log_warn() { printf '[isrt] warning: %s\n' "$*" >&2; }
log_error() { printf '[isrt] error: %s\n' "$*" >&2; }
die() {
  log_error "$*"
  exit 1
}
