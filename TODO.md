# TODO

## Not exercised yet on real targets

- [ ] Uninstalling an app. The commands are written, but only ever shown, never run here.
- [ ] Services: start, stop, restart, enable and disable from the page, on the system bus. The same calls work on a throwaway user service.
- [ ] Users: lock and sign out.
- [ ] Disk health, which needs `smartctl`.
- [ ] Installing the `.deb` on Debian or Ubuntu, and using the AppImage on an older system.
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

- [ ] App History can't count a process's last update, because it has exited by the next one, so short runs read up to one update low.

## Later features

- [ ] Update checks. There is no release address yet, and the app makes no network requests. Any future request needs a generic User-Agent and no personal data.
