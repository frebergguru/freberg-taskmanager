# Skin format

A skin sets the look of the popout. It is data and can't run anything: a JSON manifest, plus images if you want them. Anything you leave out comes from the default skin, so a skin only needs what it changes.

## Where skins live

`~/.local/share/Freberg.guru/Freberg.guru - Task manager/Skins/`

Right-click the popout and choose **Skin ▸ Open Skins Folder**. After adding or changing a skin, choose **Skin ▸ Reload Skins**. No restart is needed. If a selected skin is removed, the popout falls back to the first built-in one and keeps your choice for when it returns.

## Two shapes

- **One file:** colours only, named `something.fgskin` or `something.json`.
- **A folder:** named `something.fgskin`, holding `skin.json` and any images. Images are named by file name only, and can't be outside the folder.

## Required keys

| Key | Meaning |
|---|---|
| `format` | Must be `1` |
| `id` | Letters, digits, `.`, `-`, `_`, up to 64 characters. Keep it the same across versions, since the choice is remembered by id |
| `name` | What the menu shows |

`author` and `description` are optional. The description is shown as a tooltip.

## Optional groups

```json
{
  "format": 1,
  "id": "example.midnight",
  "name": "Midnight",
  "surfaces": {
    "window": {"top": "#101830", "bottom": "#000010"},
    "header": "#203060",
    "tile": "#000020",
    "windowImage": {"file": "stars.png", "fit": "fill", "opacity": 0.5}
  },
  "colours": {"ink": "#e0e8ff", "inkDim": "#8090c0", "unlit": "#202840", "outline": "#304070"},
  "accent": {"cpu": "#ffb000", "memory": "#60c0ff"},
  "fonts": {"body": {"size": 9, "weight": "bold", "family": "Liberation Sans"}},
  "metrics": {"corner": 4, "border": 2, "padding": 4, "gap": 2},
  "header": {"title": "Midnight", "hideTitle": false},
  "meters": {"style": "bars", "segments": 16, "flat": false},
  "quadrants": ["cpu", "memory", "gpu", "thermal"],
  "glow": 4,
  "shape": {"kind": "rectangle", "frameInset": 0}
}
```

- **Colours:** `#rrggbb` or `#rrggbbaa`, or `clear` or `none`.
- **Surfaces:** a colour, or `{"top", "bottom"}` for a vertical gradient. `window`, `header` and `tile` can be set, and `windowImage` and `headerImage` can be placed on the first two.
- **Images:** `file`, `fit` (`fill`, `fit`, `stretch` or `tile`) and `opacity`. PNG and JPEG are always available; other formats depend on the Qt image plugins installed.
- **Fonts:** `size` is clamped to 5-24 points.
- **Meters:** `style` is `bars`, `gauge`, `graph`, `vertical` or `rows`.
- **Accent:** the lit colour per channel: `cpu`, `memory`, `gpu`, `thermal`, `disk`, `network`, `power`, `alert`.
- **Quadrants:** which readings appear, in order. Any channel except `alert` is allowed. Unknown names are dropped, and if none remain the default four are used.
- **Glow:** 0-24.
- **Shape:** `kind` is `rectangle`, `circle`, `ellipse`, `capsule`, `polygon` (with `points`, a list of `[x, y]` from 0 to 1) or `mask` (with `file`, an image whose transparency cuts the window). Other shapes usually need `frameInset` to leave room for the content.

## Limits

- Manifest: up to 256 KB.
- Each image: up to 8 MB, 4096 pixels on a side, and 4 megapixels. An image over these is skipped, and the skin still loads.
- Only the first 64 entries of the folder are read.
- Values out of range are clamped, not refused.

Problems are listed with their reasons under **Skin ▸ Skin Problems**. Common ones are invalid JSON, a missing id or name, a `format` other than 1, and an id or name already in use.

## What skins can't do

Run code, read files outside their own folder, use URLs, use the network, change what the popout measures, or take the id of a skin already loaded.

## Sharing

Zip the `.fgskin` folder and send it. The recipient unzips it into their skins folder and reloads.
