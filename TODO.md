# TODO

## Not exercised yet on real targets

- [ ] Uninstalling an app. The commands are written, but only ever shown, never run here. That includes the Debian one, which now uses `dpkg -r`.
- [ ] Services: start, stop, restart, enable and disable from the page, on the system bus. These calls no longer block the window, and that new code has only been built and run through the tests.
- [ ] Startup: turning a user service on or off, which uses the same new calls.
- [ ] Users: lock and sign out, also through the new calls.
- [ ] Disk health, using Check Health on a disk's page. `smartctl` is installed here now, and it asks for a password.
- [ ] A trial run of `tools/release.sh`, whose checks and `.deb` dependencies changed after 0.1.0. It refuses to run until the version in `CMakeLists.txt` is raised, because `v0.1.0` is tagged on older code.
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

- [ ] Build the AppImage on an older system. It needs glibc 2.43 today, so it only starts on very new ones.
- [ ] Decide on the glow in the neon skins. `app/popout/popout.cpp` draws it outside `glowStroke()`, whatever the theme, which bends the rule that only the Neon Glow theme glows. Either allow it in `AGENTS.md` or draw it through the helper.
- [ ] Ship the MIT and BSD license texts of the AppImage runtime (type2-runtime, squashfuse), with their authors' names. `tools/third-party-notices.sh` copies only what is on the build machine, and the notice points to the source for the rest.
- [ ] Sorting from the keyboard on lists other than Processes.
- [ ] Connections: the refresh walks every process's files on the window's thread. It can be slow with auto-update on and thousands of open files. The lists on Services and Users also read D-Bus on the window's thread.
- [ ] App History can't count a process's last update, because it has exited by the next one, so short runs read up to one update low.

## Later features

- [ ] Update checks. Releases are on GitHub, but the app makes no network requests today. Any future request needs a generic User-Agent and no personal data.
