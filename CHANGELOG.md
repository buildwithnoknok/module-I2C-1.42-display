# Changelog - noknok Display module firmware

Newest first. One entry per version bump, written in the same commit as the bump. Format:
`## <version> - <date>` then `- DEV-xx - what changed and why. Breaking: yes/no.`
Convention: Confluence "Development Conventions" (SD space, page 120160257).

## Full image - 2026-11-19

- DEV-197 - new `firmware/bin/display_full.bin`: the full flash image (stage-0 + stage-1 1.2.0 + display firmware 0.7.1 + valid-app marker) for a first flash or recovery over SWD, recorded in `firmware/full.json`. Built from released parts by the new `tools/mk_full_image.py`; `--check` fails when the full image no longer matches `firmware/index.json`, which still points at the app-only OTA image. README: how to flash it, and why the app image must never go to `0x08000000`. No firmware change. Breaking: no.

## Tools - 2026-11-17

- DEV-77 - the bring-up / vendor-init / brand-purple reproduction scripts are now in `tools/bench/bringup/` (were only on a bench host). Tools only, no firmware change. Breaking: no.

## 0.7.1 - 2026-10-23

- DEV-44 - I2C slave hardening: the error byte is read-to-clear (a status read returns it and resets it
  to 0); a STOP without an address match is ignored, so a host bus deinit no longer re-runs the previous
  command (the stray error 4); each finished command is copied into its own buffer at STOP, so a status
  read can no longer wipe a command that has not run yet. Protocol unchanged. Breaking: no. Note: noknok.py polls
  status before every draw, so `d.status()` now reports errors since the driver's last poll (normally the
  last command), no longer everything since power-on. Built with ch32fun + riscv64-unknown-elf-gcc 14.2.0 on
  the Pi4RFID bench.

## Baseline - 2026-10-02

- Changelog starts here. Current: display firmware 0.7.0 (released 2026-10-01). Earlier history: git log and the README.
