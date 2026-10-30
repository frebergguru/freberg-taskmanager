# TODO

## Decisions needed from the owner

- [ ] Choose a license. There is none yet, so the project is all rights reserved, and no license file ships in the packages.
- [ ] Decide on update checks. The menu item says no release address is set, and the app makes no network requests.

## Not exercised yet on real targets

- [ ] Uninstalling an app. The commands are written, but only ever shown, never run here.
- [ ] Services: start, stop, restart, enable and disable from the page, on the system bus. The same calls work on a throwaway user service.
- [ ] Users: lock and sign out.
- [ ] Disk health, which needs `smartctl`.
- [ ] Installing the `.deb` on Debian or Ubuntu, and using the AppImage on an older system.
- [ ] `tools/grant-sensor-access.sh`.

## Missing or partial

- [ ] NVIDIA GPU readings (no hardware here to test on).
- [ ] Intel GPU busy percentage. Only speed is read at present.
- [ ] GPU column in Processes on real GPU work, tested so far only with fixtures.
- [ ] Network per process counts TCP only, and App History has no network figure.
- [ ] Wi-Fi network name (needs nl80211). Signal and quality work.
- [ ] Services without systemd are listed but can't be controlled.
- [ ] Process names longer than 15 characters are cut off by the kernel ("systemd-userwor"). Take the full name from the command line.
- [ ] Group processes by container, beyond the sortable Group column.
- [ ] Keyboard route to the Disk Space map.
- [ ] Sortable columns in the Thermals table.
- [ ] Thermal pressure on AMD. The kernel has no throttle counter there, so it shows "Not supported".
- [ ] Recording that starts by itself at launch.
- [ ] ARM64 packages (no cross toolchain or hardware here).
- [ ] Backends for other operating systems. `platform/backend.h` is ready, but `app/system/dbus.*` and parts of `app/views` still assume Linux.

## Resource targets still missed

Measured offscreen, on a busy machine:

- [ ] Total memory is 47-48 MB against a 45 MB target. Only about 9 MB is the app's own, and the rest is shared Qt libraries.
- [ ] Performance page CPU is 0.53% against 0.5%. Redrawing graphs less often is the next thing to try.

## Smaller things

- [ ] Memory modules: a slot whose size the firmware reports as unknown shows as empty. Needs a fixture, since the table is root-only here.
- [ ] App History leaves out a process's first and last update, so short runs read low: a 6 s job showed 5.0 s.
- [ ] The saved last page is a number, so a changed page order opens the wrong page once.
- [ ] Modern: the sidebar leaves less room for Processes' columns at the default window size.
- [ ] Performance also has a small Thermals page, now that a full one exists. Keep it, or link to the new one?
