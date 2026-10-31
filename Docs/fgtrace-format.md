# The .fgtrace recording format

Version 1. An open format, so other tools can read recordings. All numbers are little endian, whatever the machine.

## File

| Field | Type | Notes |
|---|---|---|
| magic | 8 bytes | `FGTRACE\n` |
| version | u32 | 1 |
| channels | u32 | How many channel values each frame holds. Currently 12 |
| frames | u64 | How many frames follow |
| frame... | | Repeated |

## Frame

| Field | Type | Notes |
|---|---|---|
| time | u64 | Milliseconds on a monotonic clock. Only differences mean anything |
| memory total | f32 | Bytes |
| channel values | f32 x channels | In the order below |
| process count | u32 | At most 64 |
| process... | | Repeated: the busiest processes in this frame |

## Process

| Field | Type | Notes |
|---|---|---|
| name length | u32 | At most 4096 |
| name | bytes | UTF-8, not terminated |
| pid | u32 | |
| CPU | f32 | Percent of the whole machine |
| memory | f32 | Resident bytes |

## Channels, in order

| # | Key | Unit |
|---|---|---|
| 0 | `cpu_percent` | % |
| 1 | `memory_used_bytes` | bytes |
| 2 | `disk_read_bytes_per_second` | bytes/s |
| 3 | `disk_write_bytes_per_second` | bytes/s |
| 4 | `network_receive_bytes_per_second` | bytes/s |
| 5 | `network_send_bytes_per_second` | bytes/s |
| 6 | `gpu_percent` | % |
| 7 | `cpu_temperature_celsius` | °C |
| 8 | `cpu_power_watts` | W |
| 9 | `pressure_cpu_percent` | % |
| 10 | `pressure_memory_percent` | % |
| 11 | `pressure_io_percent` | % |

## Rules

- **A value that wasn't measured is NaN.** It means a gap, never 0. CSV export leaves those cells empty, and JSON export writes `null`.
- **Readers must accept more channels than they know,** and skip the extra ones. If a file has fewer, the rest are gaps.
- **Damaged files are refused,** not guessed at: a wrong magic, a newer version, a count past the limits above, or a file that ends early. This reader also stops at more than 1024 channels or 100,000,000 frames, a timestamp past 2^63, and a value that is infinite, negative (only temperature may be) or past 1e18.
- **Files are written to `<name>.part`** and renamed when complete, so a file with the real name is whole.

Exports use a decimal point and field names as above, whatever the system's language.
