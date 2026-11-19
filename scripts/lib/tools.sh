#!/usr/bin/env bash
# Tool lookup. Source this file; it defines functions only.

# have <command>: true when the command is on PATH.
have() { command -v "$1" >/dev/null 2>&1; }

# need <command> [hint]: stop with a hint when the command is missing.
need() {
  have "$1" && return 0
  die "$1 is required${2:+ ($2)}"
}

# run_tool <command> <docker image> [args...]: run the command from PATH, or
# from the image with the repository mounted at /src when it is missing.
run_tool() {
  local cmd="$1" image="$2"
  shift 2
  if have "$cmd"; then
    "$cmd" "$@"
  elif have docker; then
    # Images that set the tool as their entrypoint take only the arguments.
    local entry
    docker image inspect "$image" >/dev/null 2>&1 || docker pull -q "$image" >/dev/null
    entry="$(docker image inspect -f '{{join .Config.Entrypoint " "}}' "$image")"
    if [[ -n "$entry" ]]; then
      docker run --rm -u "$(id -u):$(id -g)" -v "$ISRT_ROOT:/src" -w /src "$image" "$@"
    else
      docker run --rm -u "$(id -u):$(id -g)" -v "$ISRT_ROOT:/src" -w /src "$image" "$cmd" "$@"
    fi
  else
    die "$cmd is not installed and docker is not available"
  fi
}
