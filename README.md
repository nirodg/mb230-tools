# MB230 USB Injection Tools

Collection of tools used with the MB230 USB injection / jailbreak workflow.

## Contents

- [`dropbear/`](dropbear/) — Dropbear SSH server/client port for Vestel MB230
  (static ARMv7 binaries, on-TV installer, host keys in `/conf/dropbear`).
  Submodule: [nirodg/mb230-dropbear](https://github.com/nirodg/mb230-dropbear).

## Quick install (Dropbear SSH on TV)

From a root shell on the TV (e.g. telnet):

```sh
mkdir -p /tmp/dropbear-pkg && cd /tmp/dropbear-pkg
TAG="v2026.94-mb230"
BASE="https://raw.githubusercontent.com/nirodg/mb230-dropbear/$TAG"
wget -O install.sh  "$BASE/install.sh"
wget -O dropbear    "$BASE/out/dropbear"
wget -O dropbearkey "$BASE/out/dropbearkey"
chmod +x install.sh dropbear dropbearkey
sh install.sh
```

See [`dropbear/README.md`](dropbear/README.md) for full options (public key
install, persistence, manual/`nc` copy, build).

## Repository layout

- This repo: [nirodg/mb230-tools](https://github.com/nirodg/mb230-tools)
- Included in: [nirodg/mb230-vestel](https://github.com/nirodg/mb230-vestel) as
  `usb-injection/tools/`
