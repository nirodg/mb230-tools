# MB230 USB Injection Tools

Collection of tools used with the MB230 USB injection / jailbreak workflow.

## Contents

- [`dropbear/`](dropbear/) — Dropbear SSH server/client port for Vestel MB230
  (static ARMv7 binaries, on-TV installer, host keys in `/conf/dropbear`).
  Submodule: [nirodg/mb230-dropbear](https://github.com/nirodg/mb230-dropbear).
- [`hello-world/`](hello-world/) — minimal ARMv7 "Hello World" proof app for
  telnet/root shell testing (prebuilt binary, Docker build).
  Submodule: [nirodg/mb230-hello-world](https://github.com/nirodg/mb230-hello-world).
- [`gui-hello-world/`](gui-hello-world/) — framebuffer GUI "Hello World" +
  remote/input test app for MB230 (prebuilt binary, Docker build).
  Submodule: [nirodg/mb230-gui-hello-world](https://github.com/nirodg/mb230-gui-hello-world).

## Quick install: Dropbear SSH (on TV, seamless)

From a root shell on the TV (e.g. telnet), pass your PC SSH public key (RSA or ed25519) to auto-create `authorized_keys`:

```sh
mkdir -p /tmp/dropbear-pkg && cd /tmp/dropbear-pkg
TAG="v2026.94-mb230"
BASE="https://raw.githubusercontent.com/nirodg/mb230-dropbear/$TAG"
wget -O install.sh  "$BASE/install.sh"
wget -O dropbear    "$BASE/out/dropbear"
wget -O dropbearkey "$BASE/out/dropbearkey"
chmod +x install.sh dropbear dropbearkey
# paste your key from PC: cat ~/.ssh/id_rsa.pub (or id_ed25519.pub)
PUBKEY='ssh-rsa AAAA... user@pc' sh install.sh
# or interactive: sh install.sh  (prompts for key)
```
Then: `ssh root@<tv-ip>`. See [`dropbear/README.md`](dropbear/README.md) for full options and persistence.

## Quick install: hello-world (on TV)

```sh
TAG="v1.0.0"
BASE="https://raw.githubusercontent.com/nirodg/mb230-hello-world/$TAG"
cd /tmp && wget -O hello "$BASE/hello" && chmod +x hello && ./hello
```

See [`hello-world/README.md`](hello-world/README.md).

## Quick install: gui-hello-world (on TV)

```sh
TAG="v1.0.0"
BASE="https://raw.githubusercontent.com/nirodg/mb230-gui-hello-world/$TAG"
cd /tmp
wget -O gui_hello "$BASE/gui_hello"
wget -O run-on-tv.sh "$BASE/run-on-tv.sh"
chmod +x gui_hello run-on-tv.sh
# takeover test (stops stock UI): ./run-on-tv.sh
```

See [`gui-hello-world/README.md`](gui-hello-world/README.md).

## Repository layout

- This repo: [nirodg/mb230-tools](https://github.com/nirodg/mb230-tools)
- Included in: [nirodg/mb230-vestel](https://github.com/nirodg/mb230-vestel) as
  `usb-injection/tools/`
