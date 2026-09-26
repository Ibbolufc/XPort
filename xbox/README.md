# XPort for Xbox Series X|S

The UWP (x64) app that runs XPort on an Xbox in Developer Mode. Current state: **Milestone 1, the app shell**
(see [../ROADMAP.md](../ROADMAP.md)). It renders a status screen, shows the live controller → PlayStation mapping,
tests rumble, and can probe the LAN for PS4/PS5 consoles. It does not stream yet.

## Build

Requirements (Windows 10/11):

- Visual Studio 2022 with **Universal Windows Platform development** and **C++ (v143) Universal Windows Platform tools**
- Windows SDK 10.0.19041 or newer
- CMake 3.20+ on `PATH`

```powershell
.\xbox\build.ps1            # configure + build + signed package (Release)
.\xbox\build.ps1 build      # compile only
.\xbox\build.ps1 -Config Debug
```

The package lands in `build-xbox\AppPackages\XPort_<version>_x64_Test\` with its `Dependencies\x64\` folder.
The first packaging run creates a self-signed `CN=XPort Dev` certificate in `xbox\.cert\` (git-ignored).

You can also open `build-xbox\XPort.sln` in Visual Studio after `.\xbox\build.ps1 configure` and deploy/debug to a
local PC (the package also targets `Windows.Universal`) or to the Xbox as a *Remote Machine*.

## Install on the Xbox

1. Put the console in Developer Mode and open **Dev Home**. Enable the **Device Portal** and note its address
   (`https://<xbox-ip>:11443`).
2. In Device Portal: **Home → Add**, pick `XPort_<version>_x64.msix`, then **Next**, add every file from
   `Dependencies\x64\`, then **Start**.
3. In Dev Home, highlight XPort, press **View** (or Menu) → **View details** and set **App type** to **Game**.
   The default "App" type gets only a small slice of CPU, GPU and memory.
4. Launch XPort from Dev Home.

## Controls (Milestone 1)

| Input | Action |
|---|---|
| Y | Search the local network for PS4/PS5 consoles |
| X | Rumble test (body motors) |
| LT / RT | Impulse-trigger rumble test |
| B | Handled (does not leave the app) |
| Hold View + Menu (3 s) | Exit |

## Logs

The app writes `LocalState\xport.log` (truncated at each launch) and mirrors every line to `OutputDebugString`.

- Device Portal: **File explorer → LocalAppData → XPort_… → LocalState → xport.log**
- Visual Studio: attach the debugger; lines appear in the Output window.

The last few lines are also shown on screen.

## Code map

| File | Purpose |
|---|---|
| `src/App.cpp` | `IFrameworkView`: lifecycle, main loop, input handling, screen layout |
| `src/Renderer.*` | D3D11 device + CoreWindow swap chain, Direct2D/DirectWrite drawing, device-lost handling |
| `src/NetProbe.*` | Phase 1 network spike: PS4/PS5 discovery broadcast with Winsock |
| `src/Log.*` | File + debugger logging |
| `Package.appxmanifest.in` | Package identity, device families (`Windows.Universal`, `Windows.Xbox`), capabilities |
| `Assets/` | Tile and splash images (placeholders until the XPort logo lands) |
| `build.ps1` | Configure / build / package / sign |
