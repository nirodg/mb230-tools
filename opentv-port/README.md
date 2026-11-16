# OpenTV MB230 port (tryable sidecar)

Early, **tryable** port scaffold to run OpenTV-style userspace on a Vestel **17MB230** (Novatek NT72671/NT72673, ARMv7 Cortex-A7, Linux 4.9 "Kylo") in a **CyanogenMod-like** model:

- **Keep vendor base:** stock kernel, drivers, and low-level/vendor libraries (DirectFB/Mali/NVT media/platform stack) remain in place.
- **OpenTV on top as sidecar:** this package installs a small ARM userspace binary + keeper that can run alongside the stock system, with a path toward replacing/augmenting the UI layer via MB230-specific backends (display/input/liveness).

Reference prototype: `/home/dorin/Downloads/open-tv-launcher` (`os/` gateway + UI/launcher). QEMU is for testing there; MB230 needs vendor-native backends and liveness-aware integration.

> **Status: v0.1.4 + window-overlay milestone (experimental).** Safe default is **sidecar mode** (no kill of `aurora.elf`). **Rendering on top proven two ways:** (1) `otv_port` DirectFB layer-surface draw on primary layer 0 (v0.1.4, visible but contends with aurora repaints), and (2) **sawman window overlay with corrected `DFBWindowDescription` layout** (RE of `IDirectFBDisplayLayer_CreateWindow` @ 0x4b358244, vendor `libdirectfb-1.7.so.1`) — on-device confirmed **visible and persistent across remote IR input and channel changes** on layer 0 with stacking=UPPER (e.g. `probe_dfb_window_desc 6 1 0x1 0 0 2`; "Change source" popup can appear on top). **Caveat:** intermittent DirectFB/fusion-related reboots observed in some states; exit/stability not fully characterized. Full takeover/liveness-bypass remains open. See `docs/REBOOT_AND_LIVENESS.md`.

## Contents

- `src/otv_port.c` — minimal OpenTV port shell with **DirectFB overlay backend** (`--dfb`, default) joining fusion/sawman to render on primary layer surface, plus fbdev fallback (`--fbdev`), UART IR/GPIO input probe, optional `/dev/watchdog` kicker, logging.
- `otv_port` — **prebuilt** dynamic ARMv7 EABI5 hard-float binary (dlopen of vendor DirectFB/NDAL libs).
- `src/ndal_wdt_keep.c` / `ndal_wdt_keep` — experimental, safe-by-default NDAL STBC WDT helper (dlsym-only; `--kick` risky without proper context).
- `build-docker.sh` / `Makefile` — reproducible Docker/native cross-build (dynamic, `-ldl`).
- `installer.sh` — on-TV installer to `/conf/opentv-port` (persistent), starts keeper, can arm auto-load hook.
- `run-on-tv.sh` — manual runner (sidecar/takeover).
- `docs/REBOOT_AND_LIVENESS.md` — reboot-on-kill evidence and bypass strategies.
- `docs/device.conf.mb230.example` — planned MB230 gateway/device mapping for full `os/` port.

## Quick install (on TV, via root telnet)

Prereq: root shell on TV (e.g. `telnet <tv-ip> 23`) and a way to transfer files (wget/LAN HTTP or `nc`).

```sh
mkdir -p /tmp/otv-port-pkg && cd /tmp/otv-port-pkg
# copy installer.sh and otv_port into this directory, then:
chmod +x installer.sh otv_port
sh installer.sh
```

Installer behavior (idempotent, defaults):
- installs to **`/conf/opentv-port`** (persistent UBIFS) and writes `mode.env`, `keeper.sh`, `run-on-tv.sh`
- starts background keeper now and (if `AUTOLOAD=1`, default) appends a **guarded, reversible** hook to `/conf/opentv_payload` **only when it is a plain shell script** (backup: `/conf/opentv_payload.bak.otvport`)
- default **`MODE=sidecar`**: runs alongside stock `aurora.elf` (no process kills)

Logs: `/tmp/otv_port.log`, `/conf/opentv-port/keeper.log`, `/conf/opentv-port/otv_port.stdout.log`.

## Run modes

### Sidecar (default, recommended first)

```sh
/conf/opentv-port/run-on-tv.sh sidecar
# or: /conf/opentv-port/otv_port            # DirectFB overlay (default, renders on screen alongside aurora)
/conf/opentv-port/otv_port --fbdev        # raw fbdev fallback
```

- Does **not** kill `aurora.elf`/idtv/appmgr (preserves platform liveness/watchdog kicks).
- **Display (proven):** joins existing DirectFB fusion/sawman session and renders to the primary display layer surface (`--dfb`, default) — visible on screen without reboot. Falls back to fbdev if DirectFB init fails.
- **Input:** probes `/dev/ttyS2` (UART IR) and `/dev/ntgpio` (non-blocking). Input events may be unavailable/conflicted while aurora owns IR.

### Takeover (experimental, may reboot)

```sh
/conf/opentv-port/run-on-tv.sh takeover
# or set MODE=takeover in /conf/opentv-port/mode.env for keeper behavior
```

- Stops `aurora.elf`/idtv/appmgr and runs `otv_port --kick-wdt` as foreground UI.
- **Risk:** killing aurora can stop MCU/STBC watchdog refresh; device may **reboot in ~20–30s** if platform watchdog is not satisfied. `--kick-wdt` best-effort pets Linux `/dev/watchdog*` if present, but the controlling WDT may be MCU/STBC-only. Keep telnet/USB recovery available. Do not use unattended until validated.

## Auto-load

- `installer.sh` (default `AUTOLOAD=1`) adds a guarded block between `# --- OPENTV_PORT_HOOK_BEGIN/END ---` to `/conf/opentv_payload` when it is a shell script, starting `keeper.sh` once in background (similar to Dropbear keeper hook). **Remove that block** or set `MODE=disabled` handling / uninstall to revert.
- If `/conf/opentv_payload` is absent or binary, hook manually from your boot flow: `INSTALL_DIR=/conf/opentv-port sh /conf/opentv-port/keeper.sh &` (ensure it runs in a long-lived keeper context).
- Keeper defaults to **sidecar** to avoid boot/reboot loops.

## Reboot prevention / render-on-top (current understanding)

See **`docs/REBOOT_AND_LIVENESS.md`** for evidence-backed analysis. Summary:

- **Primary liveness:** aurora kicks MCU/STBC watchdog via `PLAT_SYS_KickWatchDog` → `NDAL_VideoExt_STBC_FreshWDT` and participates in appmgr/privserver/semaphore/STR flow. Killing aurora removes kicks → likely MCU WDT reset.
- **V0.1 mitigation attempts:** `--kick-wdt` pets `/dev/watchdog*` if available (best-effort; may not be the same WDT). NDAL STBC kick via `libNDAL_EX.so` requires valid context/init (unsafe uninitialized calls correlated with reboot). True bypass likely requires keeping aurora alive, or a context-correct WDT shim plus appmgr contract handling.
- **Render-on-top / priority (proven, sidecar):** `otv_port --dfb` joins the running DirectFB 1.7 fusion+sawman session and draws to the primary display layer surface — **visible on screen alongside aurora** (no kill, no reboot). Raw fbdev (`/dev/fb0-4`) writes were not scanout-visible under compositor control. Next: refine overlay compositing/positioning/layers/input integration and integrate into a full OpenTV UI launcher while keeping liveness intact.

## Build (reproduce binaries)

```sh
cd usb-injection/tools/opentv-port
./build-docker.sh
# or: make docker
# produces: ./otv_port, ./ndal_wdt_keep (ELF 32-bit ARM, EABI5, dynamically linked, -ldl)
```

## Mapping to OpenTV `os/` (next porting steps)

- Cross-build OpenTV `ui`/`launcher` core for **ARMv7hf** (Linux 4.9, firmware-lib compatible).
- Implement MB230 gateway backends per `docs/device.conf.mb230.example`: **DirectFB** (preferred, vendor stack) or fbdev display; **UART-IR** (`/dev/ttyS2`/`/dev/ntgpio`) input mapped to `gw_key`; storage under `/conf`/`/buffer`.
- Integrate without breaking liveness (keep aurora alive, or replace kicks/contracts). Iterate toward launcher UI parity with prototype.

## Safety / notes

- Research on owned hardware. No firmware images, dumps, or device secrets are included or should be committed.
- Do **not** modify signed boot components. Prefer reversible `/conf` changes and keep USB recovery/restore scripts available.
- `/tmp` is cleared on reboot; persistent state goes to `/conf/opentv-port`.
- `otv_port` is a v0.1 experiment, not a full launcher yet.
