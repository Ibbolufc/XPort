# XPort roadmap

The plan for turning the inherited chiaki-ng based Remote Play codebase into XPort, a PlayStation Remote Play client
for Xbox Series X|S Dev Mode. The rule throughout: **reuse the protocol implementation in `lib/`, don't rewrite
it.** The Xbox app only maps platform inputs and outputs (controllers, video, audio, storage, UI).

| Phase | Goal | Status |
|---|---|---|
| 0 | `lib/` builds and passes its unit tests with the MSVC ABI (clang-cl) | **done**: 126/126 tests pass on Windows x64 in CI |
| 1 | Minimal UWP app launches on Xbox (**Milestone 1**) | **built**: signed package from CI; waiting on the on-console check |
| 2 | `lib/` compiled for UWP and linked into the app | planned |
| 3 | LAN discovery, wake and registration | planned |
| 4 | Remote Play session with video | planned |
| 5 | Audio, full controller input, rumble/haptics | planned |
| 6 | Gamepad-first UX, settings, in-stream menu, lifecycle polish | planned |
| 7 | PSN sign-in and Internet Remote Play (holepunch/RUDP) | planned |
| 8 | HEVC/4K/HDR, latency tuning, release packaging | planned |

## Phase 0: toolchain groundwork

UWP apps must be built with the MSVC toolchain family (MSVC or clang-cl); the lib was previously only built with
MinGW on Windows.

- Done: `<strings.h>` replaced by `lib/src/compat_strings.h` (`strcasecmp` → `_stricmp` on MSVC).
- Done: `CHIAKI_LIB_FETCH_DEPS` builds json-c, miniupnpc and opus from source (default on MSVC/WindowsStore),
  because pkg-config packages don't exist there.
- Done: protoc falls back to Python `grpc_tools` when no system `protoc` is installed.
- Done: fixed `chiaki_ecdh_set_local_key` on mbedTLS (it replaced the given key with a random one).
- Done: CI build fixes for fetched deps under clang-cl (json-c `SSIZE_T` detection and generated `json.h`,
  opus SSE4.1/AVX2 kernels disabled for clang-cl).
- Exit criterion met: `chiaki-unit` passes in CI on Windows x64 with clang-cl + mbedTLS.

## Phase 1: Milestone 1, a minimal Xbox app

`xbox/`: C++/WinRT `CoreApplication`, D3D11 swap chain + Direct2D text, `Windows.Gaming.Input` controller readout
(with the planned PlayStation mapping), rumble/impulse-trigger test, suspend/resume/back-button handling, a log file
in `LocalState`, and a signed sideload package from `xbox/build.ps1`. It does not link `lib/` yet.

It also carries a **network spike**: pressing Y broadcasts the real PS4/PS5 discovery request (UDP 987/9302) with
Winsock and lists the consoles that answer. This checks early that UDP broadcast works for a UWP app on Xbox.

Exit criteria (on a real Xbox Series X|S in Dev Mode, app type set to "Game"):
1. The package installs through Device Portal and launches from Dev Home.
2. The screen renders; the controller readout follows input; X/LT/RT rumble.
3. Suspend/resume (Guide → Home and back) and B do not crash or exit the app; View+Menu held exits.
4. Y finds a powered-on or rest-mode PS4/PS5 on the same network.

## Phase 2: `lib/` inside the UWP app

- Configure `lib/` with `CMAKE_SYSTEM_NAME=WindowsStore`, consumed by `xbox/CMakeLists.txt` via
  `add_subdirectory` (so the app sees the lib's exact struct layout, see the warning in `session.h`).
- mbedTLS for crypto (verify its Windows entropy source is UWP-allowed; otherwise add a `BCryptGenRandom` hook),
  curl with Schannel, json-c/miniupnpc/opus via `CHIAKI_LIB_FETCH_DEPS`.
- Fix any API that is not in the UWP app partition behind guards in `lib/` (candidates: `InitializeCriticalSection`,
  `MoveFileExA`, `GetAdaptersInfo`, `SetThreadDescription`).
- Route `ChiakiLog` into the app log; call `chiaki_lib_init()` at startup.
- Exit: the app launches on Xbox with `lib/` linked and logs the lib version.

## Phase 3: discovery, wake, registration (LAN)

- `chiaki_discovery_service` with broadcast addresses from the adapter list; host list UI; `chiaki_discovery_wakeup`.
- Registration with PIN via `chiaki_regist_start`, with the PSN account ID entered manually at first
  (base64, 12 characters). PSN sign-in comes in Phase 7.
- Persist registered hosts (regist key, morning, nickname, MAC) as JSON in `LocalFolder`.

## Phase 4: session + video

- Fill `ChiakiConnectInfo`; `chiaki_session_init/start/stop/join/fini` driven from the UI.
- Decode with `lib/src/ffmpegdecoder.c` and a UWP build of FFmpeg (decoder-only, h264/hevc, `d3d11va`, LGPL),
  handing it the app's `ID3D11Device`. Fallback: Media Foundation's hardware decoder with `MF_LOW_LATENCY`, fed the
  same Annex-B buffers.
- Render NV12/P010 textures with a small YUV→RGB shader, letterboxed, with a stats overlay.

## Phase 5: audio, input, feedback

- Opus via `lib` to an XAudio2 source voice with latency trimming.
- `GamepadReading` → `ChiakiControllerState`; PS button via `chiaki_session_set_ps_chord` (Menu+View);
  touchpad click on a configurable chord.
- Rumble: `CHIAKI_EVENT_RUMBLE` → body motors. PS5 delivers rumble only as DualSense haptic audio, so move the
  haptics-PCM → amplitude conversion into `lib/` and drive the motors from it. Trigger effects → impulse triggers.

## Phase 6–8

Gamepad-first UI and settings, in-stream menu on `CHIAKI_EVENT_PS_CHORD`, on-screen keyboard, login PIN, TV safe area;
PSN OAuth moved into `lib/` (curl) then holepunch/RUDP Internet play; HEVC, 4K/HDR on PS5, frame pacing, release
packaging.

## Known risks

- UDP broadcast / inbound UDP behaviour for UWP apps on Xbox (checked by the Phase 1 spike).
- Building FFmpeg for UWP with D3D11VA (Media Foundation is the fallback).
- A PSN sign-in flow that works on a TV without a keyboard.
- UWP API-partition gaps in `lib/` that only show up when compiling with `WINAPI_FAMILY_APP`.
