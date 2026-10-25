# Changelog

This file follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Release tags must
match a section heading exactly (for example `v1.0.0`): CI uses the matching
section as the GitHub Release notes.

## [Unreleased]

## [v1.0.0] - 2026-10-25

### Added

- SD Bench, a GWHB homebrew for Retro-Go SD (firmware ABI v2): six tests of
  the SD card (sequential write and read in 64 KB blocks on a 32 MB file,
  512 byte reads, random 4 KB reads and writes, small files), a live speed
  chart for each test, a results screen with bars, results appended to
  `/sdbench.txt`. English and French.
- PC version (SDL2, `make host`), headless checks with `SDB_AUTOSTART`.
