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
- **Binary key protocol (from aurora forwarder):** 5-byte datagram = `0x4b` ('K') + 4-byte little-endian key code. `otv_port` decodes press/release: key `< 2000` = press, key `>= 2000` = release of `(key-2000)`. Helper `send_key <key>` sends a binary key.
- **Protocol guidance for forwarders:** send `KEY <mapped_id_or_name>` (one datagram per event). Example: `./send_cmd "KEY 0x21f" /tmp/otv_port.sock`.

## Working solution (validated on-device): live aurora patch forwarding decoded keys

**Result (proven on MB230):** physical remote keys are intercepted and forwarded to `otv_port` via a `ptrace`-based in-memory patch of the running `aurora.elf`.

- **Hook point:** `FUN_00761cf8` (key dispatch/route) at `0x761cf8`. Patched with a **`b.w` (no-link) 4-byte branch** to a code cave, preserving the original `stmdb sp!,{r4-r8,lr}` at `0x761cfc` (the earlier `bl` + 8-byte patch corrupted `lr`/register-save and crashed aurora → watchdog reboot).
- **Code cave:** `0x690100` (clean padding between `.gnu.version_r` and `.text`; the first attempt at `0x69003c` overlapped `.gnu.version_r` and corrupted the dynamic-linker version table).
- **Forwarder** (`src/mempatch.c` `cave_blob`): raw-syscall only (`svc` for `socket`/`sendto`/`close`, `__NR` 281/290/6) — no libc/PLT lazy-binding; writes only to the stack (message buffer) and read-only cave (sockaddr `/tmp/otv_port.sock`); preserves `r0` (key), `r1` (=0x4000 for the skipped `mov.w r1,#16384`), `r4-r7`, and original `lr`; then `b.w 0x761cfc` into the intact prologue. Sends 5-byte datagram `'K' + key(LE)`.
- **Key semantics (observed):** each physical press yields a press code (e.g. volup = `0x221`) and a release code = press + `0x7d0` (2000), e.g. `0x9f1` (matches `ice_KeyEventDispatchThread` `+2000` release offset). `otv_port` labels `KEY <code>` / `REL <code>`.
- **Tooling:** `src/mempatch.c` (static ARM binary) — `mempatch <pid> apply|restore` via `PTRACE_ATTACH`/`POKEDATA`; backs up original bytes to `/tmp/irpatch.orig`. Live/in-memory only (re-apply after aurora restart/reboot).
- **Caveats:** patch is volatile (memory only); target addresses are exact for this firmware build (`aurora.elf` sha256 `c7f1c7b5…6084b`); must match the running image. No NAND/flash changes.

## Validation / safety notes
- Sidecar must not kill aurora (liveness/watchdog/appmgr coupling). Forwarding is observational/cooperative.
- Binary patching of `aurora.elf` on-device requires backups and a recovery path; prefer reversible experiments.
- Evidence is from static RE (Ghidra exports) and on-device observations; confirm offsets/symbols against the exact running firmware image before patching.
