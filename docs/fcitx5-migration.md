# Uniiki Fcitx5 Migration

Uniiki's Python daemon backend is useful for prototyping the Telex engine, but
it cannot be made fully reliable with `pynput`. On Linux, a correct input method
should let the input method framework own the key event before text reaches the
target application.

This migration moves Uniiki toward a native Fcitx5 addon.

## Why Fcitx5

- Fcitx5 receives key events before the application commits text.
- The engine can accept/filter a key event and commit the final text itself.
- No synthetic Backspace race is needed.
- It works across GTK, Qt, Electron, browsers, terminals, and Wayland/X11 apps
  through the desktop input method stack.

## Current State

- `src/engine.py` remains the reference implementation and test oracle.
- `src/daemon.py` is now considered a fallback/prototype path.
- `fcitx5/` contains a first native addon scaffold:
  - addon metadata
  - input method metadata
  - CMake build files
  - a minimal C++ engine/state class

The C++ engine is intentionally small. The next migration step is to port the
full Telex evaluator from `src/engine.py` into `fcitx5/src/uniiki_engine.cpp`
and keep both implementations covered by the same word/sentence fixtures.

The scaffold has been configured, built, and staged successfully with
`Fcitx5Core 5.1.19`.

## Ubuntu Build Dependencies

```bash
sudo apt update
sudo apt install \
  cmake extra-cmake-modules g++ pkg-config \
  fcitx5 libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev
```

Package names can vary slightly by Ubuntu release. If `apt` cannot find one of
the `libfcitx5...-dev` packages, check:

```bash
apt search fcitx5 | grep -- -dev
```

## Build

```bash
cmake -S fcitx5 -B build/fcitx5 -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/fcitx5
```

## Install Locally

System install:

```bash
sudo cmake --install build/fcitx5
fcitx5 -r
```

Then open Fcitx5 Configuration and add `Uniiki`.

Staging install without touching the system:

```bash
cmake --install build/fcitx5 --prefix /tmp/uniiki-fcitx5-install
find /tmp/uniiki-fcitx5-install -type f
```

## Migration Checklist

- Port Python Telex evaluator to C++.
- Replace byte-counted string diff with UTF-8-aware character diff.
- Add C++ unit tests for the same fixtures as `tests/test_engine.py`.
- Implement reset on focus change, cursor movement, Backspace, Enter, Tab, and
  punctuation.
- Add status/submode action for VN/EN.
- Remove or clearly mark `pynput` daemon as experimental fallback.
