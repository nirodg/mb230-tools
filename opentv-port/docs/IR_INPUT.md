# IR / Remote Input: analysis and interception strategy (MB230 sidecar)

## Status (on-device evidence)
- TV: Vestel 17MB230 (NT72671/73, Linux 4.9 Kylo), stock `aurora.elf` (`/mtd_apexe/board/com.nvt.idtv/aurora.elf`) running as idtv/appmgr UI owner.
- `aurora.elf` holds input devices (observed): `fd 5 -> /dev/ttyS2`, `fd 16,24 -> /dev/ntgpio`. `/dev/input` exposes only `mice` (no evdev remote/key nodes); `/dev/uinput` exists but is not a source of physical remote events.
- `otv_port` (sidecar) can open `/dev/ttyS2` and `/dev/ntgpio` but receives **no events** while aurora owns/consumes them — direct sidecar snooping of the consumed UART/gpio stream is not viable.

## RE findings (stock aurora, evidence-backed)
Sources: prior Ghidra 12.1.4 headless decomp exports (`/tmp/opencode/ghidra-out/*`), `aurora-re/AURORA_MODDING_REFERENCE.md`, `AURORA_ELF_ANALYSIS.md`.

| Address | Symbol | Role (from decomp/callers) |
|---|---|---|
| `0x00840608` | `DEV_Uart_GetKey` | Reads key data from UART path (semaphore-wait/signal based; internal to `aurora.elf`, not exported from shared libs). Caller: `ice_KeyEventDispatchThread`. |
| `0x00761880` | `FUN_00761880` | Decodes raw UART/RC data by protocol/type and maps command byte via `mapVestelKey`; outputs mapped internal key id (`0x2f1` = invalid). |
| `0x007617c0` | `mapVestelKey` | Vestel key table lookup (raw command → internal key id). |
| `0x00761cf8` | `FUN_00761cf8` | Central key dispatch/route: DVD/hotel/fair-feature gates, suspend (`0x21f`) handling, routes to Netflix callback or `ice_SendKeyToMw` (middleware/UI). Many callers (dispatcher, long-press, repeat, inject/send helpers). |
| `0x00763dc0` | `ice_KeyEventDispatchThread` | Main loop: `DEV_Uart_GetKey` → decode/map (`FUN_00761880`) → long-press/repeat/hotkey/APM/profile logic → dispatch (`FUN_00761cf8`); also handles hidden digit-sequence USB trigger and special cases. |
| `0x007649c8` | `ICE_CallKeyPressFunction` | Programmatic key injection into dispatch path (per RE docs). |

**Implication:** physical IR flows through ttyS2/UART into aurora, is decoded to internal key ids, then dispatched. Interception at the **decoded dispatch point** yields semantic key events (preferred over raw UART bytes).

## Why direct sidecar interception fails (current architecture)
- ttyS2 is a serial device read/consumed by aurora; a second userspace reader cannot reliably observe the same consumed stream.
- ntgpio is likewise owned/consumed by aurora in observed state.
- No Linux evdev remote device is exposed; `/dev/uinput` can inject but not observe physical presses.

## Proper interception strategy (recommended)

### 1) Cooperative forwarding from aurora to sidecar IPC (preferred for sidecar)
Forward each dispatched (mapped) key from aurora to `otv_port`’s control socket (default `/tmp/otv_port.sock`, unix datagram). `otv_port` v0.1.7+ accepts commands such as `KEY <code|name>` and `PING/QUIT/...` and records them as input events.

- **Hook point:** `FUN_00761cf8(param_1)` (mapped key id) or immediately after decode in `ice_KeyEventDispatchThread` before/alongside dispatch. Forwarding the final dispatched key preserves semantics (including repeats/long-press-derived keys if hooked at call sites of interest).
- **Payload:** e.g. `KEY <id>` or `IR <id>` (and optionally press/release/long metadata if available).
- **Implementation options:**
  - **Binary patch of stock `aurora.elf`**: add a small thunk at `FUN_00761cf8` prologue (or a call site) to `socket(AF_UNIX,SOCK_DGRAM)+sendto("/tmp/otv_port.sock","KEY ...")` then execute original instructions. Requires ARMv7 PIC-safe shellcode/trampoline, available code cave/relocation space, and careful preservation of regs/stack; reversible by restoring original bytes. High care (validate on-device, keep backups). Functions/symbols are internal (not in shared libs), so `LD_PRELOAD` cannot interpose them.
  - **Custom/rebuilt aurora/launcher path**: if running a modifiable aurora build (e.g. stage1/custom launcher context), add forwarding in source at the dispatch hook — safest engineering path but depends on replacing/augmenting aurora.
  - **`ICE_CallKeyPressFunction`/injection symmetry**: useful for *injecting* into aurora, not for observing physical presses.

### 2) Raw UART/driver-level observation (not recommended from userspace sidecar)
- Wrapping libc `read`/`ioctl` via `LD_PRELOAD` could observe tty access *if* used, but `DEV_Uart_GetKey` is an internal implementation (semaphore/driver interaction) and raw frames still require protocol decode (`FUN_00761880`/`mapVestelKey`) to become semantic keys. Fragile and coupled to internals.

### 3) Kernel/input-layer tap
- No evdev remote device observed; would require driver/kernel changes or uinput-based re-emission from a privileged component — out of scope without kernel mods.

## Implemented in `otv_port` (receiver side)
- `--cmd-socket PATH` (default `/tmp/otv_port.sock`): unix datagram IPC; commands counted as input events, logged, displayed (`IPC` / last cmd). `send_cmd "<cmd>" [path]` helper included.
- Supported control commands: `PING`, `KEY <name|code>`, `QUIT|EXIT|STOP`, `RAISE`, `OPACITY <0-255>`, `MOVE <x> <y>` (slot guess), `RESIZE <w> <h>` (slot guess).
- **Protocol guidance for forwarders:** send `KEY <mapped_id_or_name>` (one datagram per event). Example: `./send_cmd "KEY 0x21f" /tmp/otv_port.sock`.

## Validation / safety notes
- Sidecar must not kill aurora (liveness/watchdog/appmgr coupling). Forwarding is observational/cooperative.
- Binary patching of `aurora.elf` on-device requires backups and a recovery path; prefer reversible experiments.
- Evidence is from static RE (Ghidra exports) and on-device observations; confirm offsets/symbols against the exact running firmware image before patching.
