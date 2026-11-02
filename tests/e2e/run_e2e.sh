#!/bin/sh
# End-to-end tests: drive the real motif binary against a real git repository and
# compare its output byte for byte with the checked-in expectations.
#
# Run with --update to regenerate the expected files after an intentional change.
#
#   usage: run_e2e.sh <path-to-motif> [--update]

set -eu

MOTIF=${1:?usage: run_e2e.sh <path-to-motif> [--update]}
UPDATE=${2:-}

case "$MOTIF" in
  /*) ;;
  *) MOTIF="$PWD/$MOTIF" ;;
esac

HERE=$(cd "$(dirname "$0")" && pwd)
FIXTURES="$HERE/fixtures"
EXPECTED="$HERE/expected"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# Captured output lives outside the repository under test, or motif would (correctly)
# report our own scratch files as untracked.
REPO="$WORK/repo"
OUT="$WORK/out"
mkdir -p "$REPO" "$OUT"

# Deterministic output: fixed width, no colour, no pager, no locale surprises.
export COLUMNS=76
export MOTIF_NO_PAGER=1
export NO_COLOR=1
export LC_ALL=C
export GIT_AUTHOR_NAME=Alice GIT_AUTHOR_EMAIL=alice@example.com
export GIT_COMMITTER_NAME=Alice GIT_COMMITTER_EMAIL=alice@example.com
export GIT_AUTHOR_DATE='2026-08-09T12:00:00+00:00'
export GIT_COMMITTER_DATE='2026-08-09T12:00:00+00:00'

cd "$REPO"
git init -q .
git config commit.gpgsign false

cp "$FIXTURES/v1.mscx" concerto.mscx
git add concerto.mscx
git commit -q -m "Initial concerto sketch"

failures=0
check() {
  name=$1
  shift
  actual="$OUT/$name.actual"

  # Commit ids vary per run; mask them so the comparison stays meaningful.
  "$@" 2>&1 | sed -E 's/^commit [0-9a-f]{40}$/commit <oid>/; s/^[0-9a-f]{6}  /<oid>  /' \
    > "$actual" || true

  if [ -n "$UPDATE" ]; then
    mkdir -p "$EXPECTED"
    cp "$actual" "$EXPECTED/$name.txt"
    echo "updated $name"
    return
  fi

  if [ ! -f "$EXPECTED/$name.txt" ]; then
    echo "FAIL $name: no expected output (run with --update)"
    failures=$((failures + 1))
    return
  fi

  if diff -u "$EXPECTED/$name.txt" "$actual" > "$OUT/$name.diff"; then
    echo "ok   $name"
  else
    echo "FAIL $name"
    sed 's/^/     /' "$OUT/$name.diff"
    failures=$((failures + 1))
  fi
}

# --- a transposed opening bar -------------------------------------------------
cp "$FIXTURES/v2_transposed.mscx" concerto.mscx
check status_transposed "$MOTIF" status --ascii
check diff_transposed   "$MOTIF" diff --ascii
check diff_filtered     "$MOTIF" diff --ascii --instrument violin --measures 1:1
check diff_stat         "$MOTIF" diff --ascii --stat
git commit -q -am "Raise the opening a step"

# --- an instrument removed ----------------------------------------------------
cp "$FIXTURES/v3_no_oboe.mscx" concerto.mscx
check status_orchestration "$MOTIF" status --ascii
check diff_orchestration   "$MOTIF" diff --ascii --orchestration
git commit -q -am "Remove the oboe"

# --- history ------------------------------------------------------------------
check log_compact "$MOTIF" log --ascii
check log_detail  "$MOTIF" log -1 --ascii

# --- a clean tree -------------------------------------------------------------
check status_clean "$MOTIF" status --ascii
check diff_clean   "$MOTIF" diff --ascii

# --- passthrough and errors ---------------------------------------------------
check passthrough_branch "$MOTIF" branch --show-current
check unknown_option     "$MOTIF" diff --nonsense

if [ -n "$UPDATE" ]; then
  exit 0
fi

if [ "$failures" -gt 0 ]; then
  echo "$failures end-to-end check(s) failed"
  exit 1
fi
echo "all end-to-end checks passed"
