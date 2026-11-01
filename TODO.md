# TODO

## Not exercised yet on real targets

- [ ] Uninstalling an app on systems other than this one. The pacman command removed a throwaway package here, but the `dpkg`, `rpm`, `flatpak` and `snap` ones have never run. Nor has the page's own button.
- [ ] Services: the page's own buttons. Its start, stop, restart, enable and disable calls all worked on a throwaway system service, with a password prompt, but were run without the page.
- [ ] Users: lock and sign out, also through the new calls.
- [ ] Check Health's own button. Its `smartctl` command, with a password prompt, read an NVMe disk's health here.
- [ ] Installing the `.deb` on Debian or Ubuntu, and using the AppImage on an older system. The `.deb` asks for the Qt it was built with (6.11.2 today), so it only installs where Qt is that new.
- [ ] `tools/grant-sensor-access.sh`.

## Missing or partial

- [ ] A real screen reader over AT-SPI. So far only Qt's accessibility tree is tested.
- [ ] Find what triggers "This plugin supports grabbing the mouse only for popup windows". The app never asks for a grab, so the exact text is filtered in `app/main.cpp`. To trace it, run without the filter using `QT_FORCE_STDERR_LOGGING=1 QT_MESSAGE_PATTERN='%{message} @@ %{backtrace depth=14}'`.
- [ ] NVIDIA GPU readings (no hardware here to test on).
- [ ] Intel GPU busy percentage. Only speed is read at present.
- [ ] Network per process counts TCP only, and App History has no network figure.
- [ ] Services without systemd are listed but can't be controlled.
- [ ] Group processes by container, beyond the sortable Group column.
- [ ] Thermal pressure on AMD. The kernel has no throttle counter there, so it shows "Not supported".
- [ ] ARM64 packages (no cross toolchain or hardware here).
- [ ] Backends for other operating systems. `platform/backend.h` is ready, but `app/system/dbus.*` and parts of `app/views` still assume Linux.

## Resource targets still missed

Measured offscreen, on a busy machine:

- [ ] Total memory is 47-53 MB depending on the page, against a 45 MB target. Only about 9-10 MB is the app's own, and the rest is shared Qt libraries.

## Smaller things

- [ ] Check Health on a USB disk. `smartctl` stops with "Unknown USB bridge" and asks for a device type, and the page shows that text as it is, which tells the user to use an option they can't set.
- [ ] Build the AppImage on an older system. It needs glibc 2.43 today, so it only starts on very new ones.
- [ ] Connections: the refresh walks every process's files on the window's thread. It can be slow with auto-update on and thousands of open files. The lists on Services and Users also read D-Bus on the window's thread.
- [ ] App History can't count a process's last update, because it has exited by the next one, so short runs read up to one update low.

## Later features

- [ ] Update checks. Releases are on GitHub, but the app makes no network requests today. Any future request needs a generic User-Agent and no personal data.
