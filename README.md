# Freberg.guru - Task manager

A small system monitor and task manager for Linux, with the look of a classic late-1990s desktop, drawn at the sharpness of today's screens. Prefer something current? A flat Modern theme is one menu click away. It is written in C++20 with Qt 6 Widgets and is built to use as little of your computer as possible.

## What it does

| Page | What you get |
|---|---|
| Summary | Every subsystem at a glance, the busiest processes, system pressure |
| Performance | CPU (overall, per logical processor, per NUMA node), memory, disks, network, GPU, thermals, energy |
| Processes | Live tree or list, filter, sort, hide columns, end, force end, end a whole tree, suspend, resume, priority, disk priority, processor affinity, a full properties window, CSV export |
| App History | Your own per-app record of CPU and disk use (off until you turn it on) |
| System Info | Hardware and system facts, memory modules, who the app is running as |
| Startup | Autostart files and user services, enable or disable, start times |
| Users | Totals per user, sessions, lock and sign out |
| Services | systemd services: start, stop, restart, enable, disable, recent log lines |
| Power & Freq | Speed per processor, governor, energy preference, idle states, power |
| Connections | TCP, UDP and Unix sockets with their processes and byte counts |
| Installed Apps | pacman, dpkg, rpm, flatpak, snap and AppImage, with uninstall |
| Drivers | Kernel modules, what uses them, and the devices they drive |
| Disk Space | Tree and map of what fills a folder, move to trash, history of scans |
| Benchmarks | CPU, memory and disk tests with saved results |
| Flight Recorder | Record, save, open, scrub and replay; ask "why was it slow?"; compare recordings; CSV and JSON export; save automatically on a CPU spike |

Also: a small popout readout with skins, a full-screen load view (F12), a tray meter, threshold alerts, eleven themes, each in light and dark (the classic ones, Modern with no effects, and a separate Neon Glow), with an option to follow the system's light, dark and high contrast settings, text size from 100% to 200%, English and Norwegian.

## What you can rely on

- **It never shows a zero it did not measure.** Each reading is either measured, estimated (and labelled so), not supported, access denied, or still being measured.
- **Missing samples stay gaps** in graphs. Lines are never drawn across them.
- **Actions can't hit the wrong process.** A process is identified by its ID together with its start time, and actions go through a pidfd, so a reused ID is refused.
- **Nothing is sent anywhere.** There are no statistics and no identifiers. This build makes no network requests at all.

## Use of resources

Only what is on screen is read. Hidden or minimized, the app has no timers running unless you have turned on recording, alerts or the tray meter. App History, the Flight Recorder, alerts and the tray meter are all off until you turn them on. The Network and GPU columns in Processes are costlier than the others, so they are hidden by default and read only while shown.

Measure it yourself with `tools/measure-idle.sh build/freberg-taskmanager`, or turn on "Show this app's own cost" in the settings.

## Build

You need CMake 3.21 or later, a C++20 compiler and Qt 6.4 or later (Core, Gui, Widgets, DBus, Sql and LinguistTools).

```sh
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build
./build/freberg-taskmanager
```

## Recording with no window

For servers and scripts. It needs no display, and it writes the same file the app opens:

```sh
freberg-taskmanager --record trace.fgtrace --seconds 600 --interval-ms 1000
```

Without `--seconds` it records until interrupted, then saves.

## Sensors that need permission

CPU power counters and memory module details are root-only on many systems. The app never asks for root on its own, and shows "Access denied" when it can't read them. See [Docs/permissions.md](Docs/permissions.md) for how to grant read access. Reading disk health asks for your password when you press its button, and needs `smartctl` from the smartmontools package.

## Known limits

- **Network per process counts TCP only.** The kernel has no such counter for UDP, so those cells say "Not supported".
- **Energy per process is an estimate**, the process's share of the CPU package figure. Linux has no per-process power counter.
- **Processes of other users** show "Access denied" for disk use and open files unless the app runs with more rights.
- **Keeping the popout above other windows is a request** to the desktop. On Wayland the desktop decides, and it also decides where the popout goes.
- **On systems without systemd,** services are listed but can't be controlled.
- **Update checks:** no release address is set yet, so the command only says so.

## More documentation

- [Skin format](Docs/skin-format.md)
- [Recording format](Docs/fgtrace-format.md)
- [Permissions](Docs/permissions.md)
- [Accessibility statement (English)](Docs/accessibility-en.md) and [(Norsk)](Docs/accessibility-nb.md)
