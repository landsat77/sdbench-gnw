# Changelog

This file follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Release tags must
match a section heading exactly (for example `v1.0.0`): CI uses the matching
section as the GitHub Release notes.

## [Unreleased]

## [v1.1.1] - 2026-10-25

### Fixed

- Free space on exFAT cards (the firmware only knows it after a full scan):
  counted from the allocation bitmap. No more trailing comma on the `Size:`
  line when a value is missing.

## [v1.1.0] - 2026-10-25

### Added

- The card in `/sdbench.txt` (a `Card:` and a `Size:` line per run): maker
  and product name from the card identity (CID), revision, manufacturing
  date, serial number, capacity (CSD), file system, volume size and free
  space. The home screen shows the card (maker, name, size, file system).

## [v1.0.1] - 2026-10-25

### Fixed

- The numbers were missing on the console (screen and `/sdbench.txt`): the
  firmware's printf has no floating point, so they are now formatted
  without it.
- Flicker at the bottom of the screen (button bar): drawing started before
  the previous frame was on screen; it now waits for the buffer swap.

## [v1.0.0] - 2026-10-25

### Added

- SD Bench, a GWHB homebrew for Retro-Go SD (firmware ABI v2): six tests of
  the SD card (sequential write and read in 64 KB blocks on a 32 MB file,
  512 byte reads, random 4 KB reads and writes, small files), a live speed
  chart for each test, a results screen with bars, results appended to
  `/sdbench.txt`. English and French.
- PC version (SDL2, `make host`), headless checks with `SDB_AUTOSTART`.
