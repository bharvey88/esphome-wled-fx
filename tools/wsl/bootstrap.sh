#!/usr/bin/env bash
# Prepare a WSL2 Ubuntu distribution to build and run everything in this repo
# that needs a Linux toolchain: the host simulator, its sanitizer build, and the
# ESPHome host-platform snapshot harness.
#
# Safe to run again. Every step checks whether it is already done, so a re-run
# on a prepared machine takes a couple of seconds and changes nothing.
#
# Run it as root inside the distribution:
#   wsl.exe -d Ubuntu-24.04 -u root -- bash /mnt/c/.../tools/wsl/bootstrap.sh
# or, from Windows, let the wrapper do it:
#   powershell -File tools\wsl\wfx.ps1 bootstrap

set -euo pipefail

# Everything this builds lives on the Linux filesystem. Build directories and
# virtualenvs on /mnt/c are several times slower, because every file operation
# crosses the 9P filesystem bridge.
WFX_HOME="${WFX_HOME:-/root/wfx}"
REPO="${WFX_REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
ESPHOME_SRC_IN_REPO="$REPO/refs/esphome"
ESPHOME_SRC="$WFX_HOME/esphome-src"
VENV="$WFX_HOME/esphome-venv"

APT_PACKAGES=(
  build-essential   # gcc, g++, make
  cmake
  ninja-build
  clang             # the sanitizer build is happy with either, clang is the fallback
  git
  python3-venv
  python3-dev
  python3-pip
  rsync             # copies the ESPHome checkout off /mnt/c
  ffmpeg            # animated output from the snapshot harness
  pkg-config
)

log() { printf '[bootstrap] %s\n' "$*"; }

step_apt() {
  local missing=()
  for pkg in "${APT_PACKAGES[@]}"; do
    dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q '^install ok installed$' || missing+=("$pkg")
  done
  if [ ${#missing[@]} -eq 0 ]; then
    log "apt packages already present"
    return
  fi
  log "installing: ${missing[*]}"
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq
  apt-get install -y -qq "${missing[@]}"
}

step_dirs() {
  mkdir -p "$WFX_HOME"
}

step_esphome_source() {
  if [ ! -d "$ESPHOME_SRC_IN_REPO/esphome/components/snapshot" ]; then
    log "WARNING: $ESPHOME_SRC_IN_REPO has no snapshot component."
    log "         Run 'git -C refs/esphome pull' on the Windows side first."
  fi
  if [ ! -d "$ESPHOME_SRC_IN_REPO" ]; then
    log "ERROR: no ESPHome checkout at $ESPHOME_SRC_IN_REPO"
    return 1
  fi
  log "syncing the ESPHome dev checkout off /mnt/c (this is the slow step on a first run)"
  # --delete so a file removed upstream does not linger and shadow a rename.
  # .git is left behind: nothing here commits to it, and it is most of the bulk.
  rsync -a --delete \
    --exclude '.git/' --exclude '__pycache__/' --exclude '*.pyc' \
    --exclude 'tests/' --exclude '.esphome/' \
    "$ESPHOME_SRC_IN_REPO/" "$ESPHOME_SRC/"
}

step_venv() {
  if [ ! -x "$VENV/bin/python" ]; then
    log "creating the ESPHome virtualenv at $VENV"
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install --quiet --upgrade pip wheel
  fi
  # The snapshot component is not in any released ESPHome, so this has to be the
  # dev branch. An editable install means a later re-sync of the source is picked
  # up without reinstalling.
  if ! "$VENV/bin/python" -c 'import esphome' 2>/dev/null; then
    log "installing ESPHome from the dev checkout (a few minutes on a first run)"
    "$VENV/bin/pip" install --quiet -e "$ESPHOME_SRC"
  fi
  # The comparison tooling reads frames and writes sheets.
  if ! "$VENV/bin/python" -c 'import numpy, PIL' 2>/dev/null; then
    log "installing numpy and pillow"
    "$VENV/bin/pip" install --quiet numpy pillow
  fi
}

step_report() {
  log "ready"
  printf '  repo            %s\n' "$REPO"
  printf '  build root      %s\n' "$WFX_HOME"
  printf '  esphome venv    %s\n' "$VENV"
  printf '  esphome version %s\n' "$("$VENV/bin/esphome" version 2>/dev/null | head -1)"
  printf '  snapshot        %s\n' \
    "$([ -d "$ESPHOME_SRC/esphome/components/snapshot" ] && echo present || echo MISSING)"
}

step_dirs
step_apt
step_esphome_source
step_venv
step_report
