# MB230 reboot-on-kill / liveness analysis (aurora.elf) and bypass strategies

**Target:** Vestel 17MB230 (Novatek NT72671/NT72673, ARMv7, Linux 4.9 "Kylo", monolithic `aurora.elf` as `com.nvt.idtv`).  
**Goal:** prevent reboot/reset when `aurora.elf` is killed, or bypass it so an OpenTV sidecar can have priority and render on top.

> Evidence below is from static RE of the shipped `aurora.elf` (Ghidra 12.1.4 ARM:LE:32:v8 exports under `/tmp/opencode/ghidra-out`, e.g. `key_functions_decompiled.c`, `focused_functions_decompiled.c`, `key_strings.txt`) and project runtime observations (`gui-hello-world`, liveness/root-shell milestones). Treat as reverse-engineering findings; validate on-device before relying on them.

## 1. Observed behavior

- Stopping/killing stock UI processes (`aurora.elf`/idtv/appmgr) can lead to **device reboot/reset after ~20–30s** (seen in takeover experiments and debug loops). Keeping stock `aurora.elf` running as the `idtv` process (v3.12 sidecar-before-exec pattern) keeps the TV stable and satisfies platform liveness.
- Root cause is **not conclusively a single mechanism** from static evidence alone, but the dominant evidenced mechanism is **loss of hardware watchdog kicking** performed by aurora/platform code.

## 2. Evidenced mechanisms

### 2.1 MCU/STBC hardware watchdog (primary, high confidence)

Aurora integrates with an MCU/STBC watchdog via Novatek DAL/platform calls:

| Function (addr, decomp evidence) | Behavior / callees | Notes |
|---|---|---|
| `ICE_HardwareWatchdog` (0x00733fd0) | Enable/disable/control watchdog; logs `ICE_HardwareWatchdog() SUCCESS/FAILED` | Called during init paths (e.g. `initialise`/`globalStartup` region, `ICE_HardwareInit` flows). |
| `PLAT_SYS_KickWatchDog` (0x00898894) | Kicks WDT via `NDAL_VideoExt_STBC_FreshWDT`; can skip/log (`Skip PLAT_SYS_KickWatchDog()`), records time via `PLAT_OS_GetTimeMS`; may shell out via `PLAT_SYS_SystemCall` in paths | **Many callers** (broadly used across subsystems) — aurora continuously refreshes WDT while healthy. |
| `PLAT_SYS_StartWatchDog` / `PLAT_SYS_SetMcuWatchDogTimeout` (0x00898498) / `PLAT_SYS_SetWatchDogTimeout` (0x008984f0) | Configure/start/stop MCU WDT via `NDAL_VideoExt_STBC_SetWDT`, `NDAL_VideoExt_STBC_StopWDT`; can stop before reconfig (`PLAT_SYS_StopMcuWatchDog` 0x008983a0) | Timeout is platform/MCU-controlled. |
| `PLAT_SYS_StopWatchDog` (0x008983e8) | Stops MCU WDT (`NDAL_VideoExt_STBC_StopWDT`) | Callers include special flows (e.g. `initializeBrowser` at 0x00733a2c stops WDT before handing off/exiting; core-dump related paths). |
| `PLAT_SYS_GetMcuWatchDogTimeout` (0x00898698) / get APIs | Query timeout via `NDAL_VideoExt_STBC_GetMcuWatchDogTimeout` | Diagnostics/config. |
| `PLAT_SYS_ResetByHWWDT` (0x0089a244) | Orderly-ish pre-reset: mute amps, sync STBC from RTC, panel power seq off, show/force-close FS open list, umount attempts, then `NDAL_VideoExt_STBC_ResetByHWWDT` | Caller observed: `ice_WiFiMonitConnected` (0x00798064). Deliberate HWWDT-triggered reset path. |
| `PLAT_SYS_Reset` (0x008976d4) / `PLAT_SYS_PowerOffNow` (0x0089a76c) | Unmount/sync/panel/DFB release and reboot or `nvt_reboot` trigger; in some env branches `PLAT_PRIVCTRL_System("pkill -f appmgr")` + exit | Normal reboot/power-off flows (not the same as WDT expiry, but show appmgr kill is part of teardown). |

**Interpretation:** if `aurora.elf` (the process hosting these kick loops/callers) is killed and nothing else refreshes the MCU/STBC WDT, the MCU/watchdog will reset the system when its timeout expires. This matches the ~20–30s reboot symptom.

**NDAL symbols (from strings/callees):** `NDAL_VideoExt_STBC_FreshWDT`, `NDAL_VideoExt_STBC_SetWDT`, `NDAL_VideoExt_STBC_StopWDT`, `NDAL_VideoExt_STBC_ResetByHWWDT`, `NDAL_VideoExt_STBC_GetMcuWatchDogTimeout`.

### 2.2 App manager / IPC / semaphore liveness coupling (high confidence as dependency)

- `PLAT_PRIVCTRL_Init` (0x0088b14c) sets up privserver callback server `COM.NVT.PRIVSERVER.CB` and client tracking (`COM.NVT.PRIVSERVER`, `_getClient`).
- `PLAT_PRIVCTRL_System("pkill -f appmgr")` appears in `PLAT_SYS_BeforeExitIDTV` (0x008975f4, called from `CallistoActivateNewCode`), `PLAT_SYS_Reset`, `PLAT_SYS_PowerOffNow`, and `initializeBrowser` flows.
- Strings/evidence indicate semaphores/handshake: `mysem_appmgr`, appmgr IPC, IDTV semaphore/wait behavior (`wait for IDTV semaphore done` seen in runtime logs/milestones). Stock integration expects `aurora.elf` to run as the registered `idtv` app and participate in appmgr/privserver protocol and suspend/resume sequencing.
- **Effect:** even if a raw reset were avoided, breaking appmgr/idtv contracts can destabilize UI/session state, suspend/standby, updates, or trigger supervised restarts/teardown. A robust replacement must satisfy or safely decouple these contracts.

### 2.3 Other reset/liveness contributors (medium/unknown)

- **Kernel/Linux watchdog device:** `/dev/watchdog` or `/dev/watchdog0` may or may not be the control point for the same MCU WDT. `stage1/launcher/opentv_payload.c` (v3.1) attempts to open and periodically write to `/dev/watchdog*` ("pet if opened") — a useful precedent, but device presence/semantics on this firmware must be probed on-target.
- **External MCU/SoC reset paths:** STBC/MCU can enforce power/reset independently; platform-specific ioctls/DAL calls inside vendor libs (not exported to arbitrary processes) may be required.
- **Crash/exit handlers:** abnormal termination paths, signal handlers, or `nvt_trigger_update`/`trigger_reboot` mechanisms exist in strings; not the same as steady-state kill, but relevant if port crashes.
- **Init/appmgr supervision:** replacing/killing registered app may cause respawn logic or error handling depending on context.

## 3. Bypass / prevention strategies (practical options)

### Strategy A — Keep `aurora.elf` alive as the liveness owner (recommended baseline, lowest risk)

- **Idea:** do **not** kill aurora; run OpenTV as a **sidecar process** on the vendor base (CyanogenMod-style userspace layer) while aurora remains the `idtv` process that kicks MCU WDT and satisfies appmgr/privserver/semaphores/STR.
- **Pros:** preserves watchdog kicks and platform contracts; aligns with proven v3.12 stable-root-shell architecture. No reboot from missing kicks.
- **Cons:** aurora still owns display/compositor and UART IR input (`/dev/ttyS2`), so OpenTV cannot trivially "render on top" or receive remote keys exclusively without additional integration (overlay/compositing hook inside platform stack, focus handoff, or input bridging/injection).
- **Use for v0.1:** default `sidecar` mode (safe), plus groundwork for deeper integration.

### Strategy B — Replace/stop aurora but provide an external WDT kick + liveness shim (experimental)

- **Idea:** when taking over UI, run a component that **replaces aurora's WDT refresh** and (as needed) emulates required appmgr/privserver behavior.
- **Linux `/dev/watchdog` kick (best-effort):** open `/dev/watchdog` or `/dev/watchdog0` and write periodically (keep fd open). This is implemented as an option in `otv_port --kick-wdt` (v0.1) and was attempted in `stage1/launcher/opentv_payload.c`. **Caveat:** may not control the MCU/STBC WDT actually responsible for reset; must be validated (`dmesg`, device nodes, survival time).
- **Platform/MCU kick via vendor APIs:** call `NDAL_VideoExt_STBC_FreshWDT` (or equivalent) the way `PLAT_SYS_KickWatchDog` does. Requires linking against/running within an environment that can access NDAL/platform libs and devices (non-trivial from a standalone static binary without those deps/symbols).
- **Stop/disarm WDT:** `PLAT_SYS_StopWatchDog`/`NDAL_VideoExt_STBC_StopWDT` (or MCU-specific disable) could prevent timeout resets, but is **high risk** (removes safety net; behavior/permissions unknown). Only attempt if explicitly supported/safe on-device.
- **Appmgr/privserver:** may need a shim that answers expected IPC/semaphores or keeps appmgr satisfied; otherwise supervision/state issues may persist even without WDT reset.
- **Risk:** likely reboot if MCU WDT not actually satisfied; test with recovery path ready (USB scripts, power cycle).

### Strategy C — Run OpenTV "inside" the stock lifecycle (integration, harder but robust)

- **Idea:** make OpenTV the UI rendered/launched under/through the platform while aurora/liveness remains intact (e.g. as a child/view/plugin driven by existing app/launcher mechanisms, or by keeping aurora's non-UI/liveness responsibilities running and swapping UI surfaces) — requires deeper RE and stable hooks.
- **Pros:** best chance at "render on top/priority" without losing liveness.
- **Cons:** significant reverse-engineering/integration effort (DirectFB/sawman layers, input routing, appmgr contracts).

### Strategy D — Kernel/boot-level or MCU-level changes (out of scope / high risk)

- Rebuild kernel/disable watchdog drivers, or change MCU firmware/boot chain: **not recommended** here (signed boot chain, vendor components, risk of brick). Prefer userspace strategies.

## 4. Recommendations for first tryable port

1. **Default to sidecar (no kill):** install/start `otv_port` as a background sidecar with aurora running; validate process runs, logs (`/tmp/otv_port.log`, `/conf/opentv-port/*`), fbdev access, and input device visibility.
2. **Probe watchdog device on-target:** check for `/dev/watchdog*`, kernel log messages, and whether `--kick-wdt` (in a controlled test) affects reboot timing when aurora is stopped. Document results per-device.
3. **Experimental takeover only with safeguards:** use `run-on-tv.sh takeover` / keeper takeover mode (includes `--kick-wdt` option) understanding it **may still reboot** if MCU/STBC WDT is not satisfied by Linux watchdog device. Keep telnet/USB recovery available.
4. **Path to "render on top":** implement MB230 gateway backends (DirectFB/fbdev display + UART-IR input bridge) and either (a) cooperative overlay/input strategy with aurora alive, or (b) a liveness shim that truly replaces aurora's MCU watchdog kicks and appmgr contracts.

## 5. Pointers (code/exports)

- Watchdog/liveness: `PLAT_SYS_KickWatchDog` 0x00898894, `ICE_HardwareWatchdog` 0x00733fd0, `PLAT_SYS_StopWatchDog` 0x008983e8, `PLAT_SYS_ResetByHWWDT` 0x0089a244, `NDAL_VideoExt_STBC_FreshWDT/SetWDT/StopWDT/ResetByHWWDT`.
- Teardown/appmgr: `PLAT_SYS_BeforeExitIDTV` 0x008975f4, `PLAT_SYS_Reset` 0x008976d4, `PLAT_PRIVCTRL_Init` 0x0088b14c, `pkill -f appmgr`, `COM.NVT.PRIVSERVER*`, `mysem_appmgr`.
- Input/dispatch (aurora-owned): `DEV_Uart_GetKey`, `FUN_00761880`/`mapVestelKey`, `ice_KeyEventDispatchThread`, `FUN_00761cf8`.
- Prior payload experiment: `stage1/launcher/opentv_payload.c` (pets `/dev/watchdog*`).
- Project docs: `aurora-re/AURORA_MODDING_REFERENCE.md`, `aurora-re/GHIDRA_DECODE.md`, `ARCHITECTURE.md`.

## Update (v0.1.2, validated on-device): DirectFB sidecar overlay renders on screen

- **Result (proven):** a sidecar process can join the running DirectFB 1.7 fusion+sawman session (`system=fbdev,wm=sawman`, mp core) alongside aurora, obtain `IDirectFB` + primary display layer (0) surface (1920x1080), and draw/flip via `libdirectfb-1.7.so.1` — **visible on TV** (semi-transparent draw confirmed) with **no aurora kill and no reboot**. Integrated as `otv_port --dfb` (default).
- **Contrast:** raw fbdev writes to `/dev/fb0-4` (all unblanked) were not scanout-visible under compositor/plane control; the compositor-owned DirectFB surface path is the working "render on top" mechanism for sidecar mode.
- **Implication:** display priority/overlay for OpenTV sidecar is achievable without replacing aurora liveness. Remaining work: refine compositing (layer/window stacking, alpha/position), input routing/ownership, and a full launcher UI backend; true takeover still requires a context-correct liveness strategy.
- **Evidence/probes:** `src/probe_dfb*.c`, `src/probe_dfb_draw.c` (dlopen+vtable calls against vendor `libdirectfb-1.7.so.1`); key vtable slots (MB230 lib build): layer `GetSurface[9]`, `SetCooperativeLevel[11]`; layer-surface `GetSize[7]`, `Layer_Flip[20]`, `Clear[23]`, `SetColor[26]`, `FillRectangle[42]` (matched to symbol starts).
