# XPort — agent guide

XPort is a PlayStation (PS4/PS5) Remote Play client for **Xbox Series X|S in Developer Mode**, built as an x64 UWP
app. It is based on [chiaki-ng](https://github.com/streetpea/chiaki-ng): a shared C library (`lib/`) holds the
Remote Play protocol; `xbox/` is the native UWP frontend. The phased plan and current status are in `ROADMAP.md`.

## Architecture
- **DO** put protocol, streaming and feature logic in `lib/`. The Xbox app only maps platform inputs/outputs
  (controllers, video, audio, storage, UI).
- **DON'T** rewrite protocol code; reuse `lib/`. Keep `lib/` edits minimal and guarded so chiaki-ng fixes stay
  cherry-pickable (diff suspect files against `upstream/main` when debugging).
- **DO** consume `lib/` from the Xbox app through its CMake target (`add_subdirectory`), never by compiling lib
  headers under a different configuration: `ChiakiSession`'s layout depends on lib compile definitions and the
  `static inline` setters in `session.h` silently write to the wrong offset otherwise.

## Toolchain facts
- UWP needs the MSVC toolchain family. `lib/` is built with **clang-cl** (MSVC ABI); the app with MSVC via the
  Visual Studio generator and `CMAKE_SYSTEM_NAME=WindowsStore`.
- MSVC/UCRT lacks POSIX headers: use `lib/src/compat_strings.h` instead of `<strings.h>`; guard other POSIX-only
  calls with `_WIN32`/`_MSC_VER`.
- `CHIAKI_LIB_FETCH_DEPS` (default ON for MSVC/WindowsStore) builds json-c, miniupnpc and opus from source.
- Use **mbedTLS** for lib crypto on Windows/Xbox; curl uses Schannel (system trust store).

## Build / verify
- Xbox app: `.\xbox\build.ps1` (`configure|build|package|clean`); read its header, it documents deploy and logs.
- Lib + tests: see README; CI job `lib-windows-clang-cl` is the MSVC-ABI reference.
- **DO** verify Xbox changes on a real console: install via Device Portal, set app type to **Game**, read
  `LocalState\xport.log`. Compiling is not verification.
- This repo is usually edited from Linux where the Windows code can't be compiled: push and use the
  `Build XPort (Xbox)` workflow as the compile check.

## How the system works (non-obvious facts to respect)
- PS5 Remote Play delivers rumble ONLY as DualSense haptic audio: set `enable_dualsense=true` and register a
  haptics sink, then convert haptics PCM to motor amplitude. Cloud play uses classic rumble events.
- The Xbox Guide button is reserved by the OS: the PS button is the lib's PS chord (`chiaki_session_set_ps_chord`,
  OPTIONS+SHARE = Menu+View on Xbox).
- On Xbox, B raises `BackRequested`; the app must mark it handled or it navigates out.
- UWP apps must call `IDXGIDevice3::Trim()` on suspend.

## Data handling: title IDs, product IDs, entitlements
- **DON'T** infer prefix/regex patterns from the ID samples you happen to see; PSN IDs are region- and
  account-dependent.
- **DO** match structurally via `cc_stable_key()` in `lib/src/cloudcatalog_merge.c` (see also
  `normalize_apollo_game` / `normalize_title` there). Extend those helpers; don't add ad-hoc string checks elsewhere.
