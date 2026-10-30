# Changelog

## Unreleased (0.1.0)

### License

- Released under the GNU General Public License, version 3 or later. The packages include the license, and About mentions it.

### Pages

- Summary, Performance, Thermals, Processes, App History, System Info, Startup, Users, Services, Power & Freq, Connections, Installed Apps, Drivers, Disk Space, Benchmarks and Flight Recorder.
- Thermals: every sensor with its lowest, highest and limit, the margin to that limit, history for any sensor, thermal pressure and fans.
- Flight Recorder: record, save, open, scrub and replay in the open `.fgtrace` format, "why was it slow?", comparing recordings, CSV and JSON export, automatic save on a CPU spike, and recording with no window.

### Look

- Classic desktop design as the default, drawn at today's sharpness: smooth text in points, antialiased curves and vector icons.
- Modern design: flat and rounded with a sidebar, and no glow.
- Neon Glow design as its own theme.
- Eleven themes, each in light and dark, with an option to follow the system's light, dark and high contrast settings.
- Popout readout with skins, a full-screen load view (F12) and a tray meter.

### Use

- Menus open and switch when pointed at, submenus open on hover, and they close when the pointer leaves. Settings can turn this off.
- Alerts on CPU, memory and temperature.
- Wi-Fi network name and link speed, read from the kernel.
- The Help menu no longer has an update check, since the app doesn't have one yet.
- The Thermals table sorts by any column.
- The Disk Space map works from the keyboard: arrow keys, Enter and Backspace. Right-click also goes up a level.
- Flight Recorder can start recording when the app starts.
- English and Norwegian.

### Fixed along the way

- Settings under a group named `general` were lost on restart, because the INI format reserves that name.
- CSV and JSON exports broke under a decimal comma.
- A placeholder limit of 65,262 °C from NVMe drives no longer shows as a real limit.
- The desktop portal message about an unknown app ID no longer appears when running a build that isn't installed.
- Tab stops between page tabs no longer depend on the platform.
- Disks now count filesystems on encrypted, LVM and RAID volumes, and mount paths with spaces. A mount that can't be asked is no longer shown as 0 bytes.
- Set Priority, Disk Priority and Affinity now reach every thread of a process, not only its first.
- A process that calls exec now shows its new name, path and command line.
- Services no longer lists aliases as stopped when the service behind them is running.
- Connections calls a Unix socket listening only when it really accepts connections.
- Startup says when an entry is for another desktop, instead of showing it as enabled.
- Number columns in Connections, Drivers and App History sort by value.
- Opening a process from Summary now selects it, even the first time Processes opens.
- Always On Top is applied at start, and the Processes column layout is saved on exit.
- Graphs show a gap for time when nothing was measured, on every page and in the popout.
- Network totals no longer count the same traffic twice through bridges and tunnels.
- Wi-Fi quality is marked as estimated, since it assumes a scale.
- Process export leaves out GPU and network columns that weren't being measured.
- Process names longer than the kernel's 15 characters are shown whole.
- A new process counts from its first update, in Processes and App History.
- A memory slot of unknown size no longer shows as empty.
- The last page is remembered by name, so a changed page order can't open the wrong one.
- The Modern window is wider by the width of its sidebar, so Processes keeps its columns.
- The Performance page spends less on pages that aren't showing, now 0.39% CPU against 0.50%.
