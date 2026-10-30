# Changelog

## Unreleased (0.1.0)

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
- English and Norwegian.

### Fixed along the way

- Settings under a group named `general` were lost on restart, because the INI format reserves that name.
- CSV and JSON exports broke under a decimal comma.
- A placeholder limit of 65,262 °C from NVMe drives no longer shows as a real limit.
- The desktop portal message about an unknown app ID no longer appears when running a build that isn't installed.
- Tab stops between page tabs no longer depend on the platform.
