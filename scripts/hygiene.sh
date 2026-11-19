#!/usr/bin/env bash
# Refuse tracked files that carry personal data or credentials: e-mail
# addresses other than GitHub noreply, private keys, assignments of secrets.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
rc=0

mapfile -t files < <(git ls-files | grep -v '^LICENSE$')
[[ ${#files[@]} -gt 0 ]] || exit 0

if grep -nIE '[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}' -- "${files[@]}" |
  grep -vE 'users\.noreply\.github\.com|@example\.(com|org)' ; then
  echo "hygiene: e-mail addresses above" >&2
  rc=1
fi
if grep -nIE -- '-----BEGIN [A-Z ]*PRIVATE KEY' "${files[@]}"; then
  echo "hygiene: private key above" >&2
  rc=1
fi
if grep -nIE '^[A-Z_]*(KEY|TOKEN|SECRET|PASSWORD)=[^<[:space:]]' -- "${files[@]}"; then
  echo "hygiene: secret assignment above" >&2
  rc=1
fi
[[ "$rc" -eq 0 ]] && echo "hygiene: clean (${#files[@]} files)"
exit "$rc"
