#!/usr/bin/env bash
# Nightly playtest run (design §5.2).
#
# Updates a dedicated worktree to origin/revival, builds ai_arena in Release,
# runs the full matrix, and rotates old run roots. Cron/systemd user timer,
# not CI.
#
# Environment:
#   NIGHTLY_WORKTREE  dedicated git worktree to update and build in.
#                     Default: ${REPO_ROOT}/../RWE-B4-nightly. On a machine
#                     following the repo's worktree convention, set it under
#                     .RWE-B4.worktrees/nightly instead.
#   NIGHTLY_KEEP      run roots to keep, newest by name (default: 7).
#   NIGHTLY_DRY_RUN   set to 1 to print every step and touch nothing.
#   RWE_LOCAL_DATA    engine local-data root, passed through to run.py
#                     (default: ${HOME}/.rwe).
#
# Exit: 0 clean, 2 findings (a marker is left in the run root), 1 failure.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"

NIGHTLY_WORKTREE="${NIGHTLY_WORKTREE:-${REPO_ROOT}/../RWE-B4-nightly}"
NIGHTLY_KEEP="${NIGHTLY_KEEP:-7}"
DATA_ROOT="${RWE_LOCAL_DATA:-${HOME}/.rwe}"
RUNS_DIR="${DATA_ROOT}/playtest/runs"

log() { printf '[nightly] %s\n' "$*"; }

rotation_count() {
    local present=0
    if [ -d "${RUNS_DIR}" ]; then
        present="$(find "${RUNS_DIR}" -mindepth 1 -maxdepth 1 -type d | wc -l)"
    fi
    if [ "${present}" -gt "${NIGHTLY_KEEP}" ]; then
        printf '%s' "$((present - NIGHTLY_KEEP))"
    else
        printf '0'
    fi
}

if [ -n "${NIGHTLY_DRY_RUN:-}" ]; then
    log "dry-run: no changes will be made"
    log "repo root: ${REPO_ROOT}"
    if [ -d "${NIGHTLY_WORKTREE}" ]; then
        log "worktree: ${NIGHTLY_WORKTREE} (present)"
    else
        log "worktree: ${NIGHTLY_WORKTREE} (absent)"
        log "git -C ${REPO_ROOT} worktree add --detach ${NIGHTLY_WORKTREE} revival"
    fi
    log "git -C ${NIGHTLY_WORKTREE} fetch origin revival"
    log "git -C ${NIGHTLY_WORKTREE} reset --hard origin/revival"
    log "cmake -S ${NIGHTLY_WORKTREE} -B ${NIGHTLY_WORKTREE}/build -DCMAKE_BUILD_TYPE=Release"
    log "cmake --build ${NIGHTLY_WORKTREE}/build --target ai_arena -j"
    log "python3 tools/playtest/run.py --execute --binary build/ai_arena (cwd ${NIGHTLY_WORKTREE})"
    log "rotation: keep ${NIGHTLY_KEEP} newest under ${RUNS_DIR} ($(rotation_count) would be removed)"
    exit 0
fi

# (a) update the worktree to origin/revival
if [ ! -d "${NIGHTLY_WORKTREE}" ]; then
    log "creating worktree ${NIGHTLY_WORKTREE}"
    git -C "${REPO_ROOT}" worktree add --detach "${NIGHTLY_WORKTREE}" revival
fi
git -C "${NIGHTLY_WORKTREE}" fetch origin revival
git -C "${NIGHTLY_WORKTREE}" reset --hard origin/revival
HEAD_SHA="$(git -C "${NIGHTLY_WORKTREE}" rev-parse --short HEAD)"
log "building ${HEAD_SHA}"

# (b) release build
cmake -S "${NIGHTLY_WORKTREE}" -B "${NIGHTLY_WORKTREE}/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "${NIGHTLY_WORKTREE}/build" --target ai_arena -j

# (c) full matrix run, teed to a log; capture the run root and the exit code
mkdir -p "${DATA_ROOT}/playtest"
LOG="${DATA_ROOT}/playtest/nightly.log"
set +e
( cd "${NIGHTLY_WORKTREE}" && python3 tools/playtest/run.py --execute --binary build/ai_arena ) 2>&1 | tee "${LOG}"
RC=${PIPESTATUS[0]}
set -e

RUN_ROOT="$(sed -n 's/^run root: //p' "${LOG}" | head -n 1 || true)"
log "run root: ${RUN_ROOT:-<unknown>} (exit ${RC})"

# (d) branch on the run's exit code
if [ "${RC}" -eq 1 ]; then
    log "run failed"
    exit 1
fi
if [ "${RC}" -ne 0 ] && [ "${RC}" -ne 2 ]; then
    log "unexpected exit ${RC}"
    exit 1
fi
if [ "${RC}" -eq 2 ] && [ -n "${RUN_ROOT}" ]; then
    touch "${RUN_ROOT}/findings-exist"
    log "findings marker: ${RUN_ROOT}/findings-exist"
fi

# (e) rotate run roots, keeping the newest NIGHTLY_KEEP by name
if [ -d "${RUNS_DIR}" ]; then
    mapfile -t ROOTS < <(find "${RUNS_DIR}" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort -r)
    for ((i = NIGHTLY_KEEP; i < ${#ROOTS[@]}; i++)); do
        log "rotate: removing ${RUNS_DIR}/${ROOTS[i]}"
        rm -rf -- "${RUNS_DIR}/${ROOTS[i]}"
    done
fi

# (f) exit with the run's code (0 clean, 2 findings)
exit "${RC}"
