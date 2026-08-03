#!/usr/bin/env bash
set -eu

# First ask the live daemon to reload without disrupting the desktop IM proxy.
if fcitx5-remote -r; then
    exit 0
fi

# This installation is DBus-activated (there is no fcitx5.service user unit).
# Stop only the exact Fcitx5 process; fcitx5-remote then activates a fresh one.
pkill -x fcitx5 || true
fcitx5-remote -r || fcitx5 -d
