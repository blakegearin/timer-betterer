# TimerBetterer

A countdown timer for Pebble smartwatches, forked from Pebble's `pebble-timer` app.

## Features

- Choose your own accent color
- Configure list sorting, grouping, and wrapping
- Pause or start a timer on create
- Extra confirmation to avoid accidental deletions
- Customize snooze duration
- Touch input support
- Mix and match settings

## Preview

<details open>
<summary>Pebble Time 2</summary>

| Detail | List | Settings | Options | Accent Color |
| ------ | ---- | -------- | ------- | ------------ |
| ![detail](assets/screenshots/emery/01-detail.png) | ![menu](assets/screenshots/emery/02-menu.png) | ![settings](assets/screenshots/emery/03-settings.png) | ![options](assets/screenshots/emery/04-options.png) | ![color](assets/screenshots/emery/01-color-picker.png) |

</details>

<details>
<summary>Pebble 2 Duo</summary>

| Detail | List | Settings | Options | Accent Color |
| ------ | ---- | -------- | ------- | ------------ |
| ![detail](assets/screenshots/gabbro/01-detail.png) | ![menu](assets/screenshots/gabbro/02-menu.png) | ![settings](assets/screenshots/gabbro/03-settings.png) | ![options](assets/screenshots/gabbro/04-options.png) | ![color](assets/screenshots/gabbro/01-color-picker.png) |

</details>

<details>
<summary>Pebble 2</summary>

| Detail | List | Settings | Options |
| ------ | ---- | -------- | ------- |
| ![detail](assets/screenshots/diorite/01-detail.png) | ![menu](assets/screenshots/diorite/02-menu.png) | ![settings](assets/screenshots/diorite/03-settings.png) | ![options](assets/screenshots/diorite/04-options.png) |

</details>

<details>
<summary>Pebble Time Round</summary>

| Detail | List | Settings | Options | Accent Color |
| ------ | ---- | -------- | ------- | ------------ |
| ![detail](assets/screenshots/chalk/01-detail.png) | ![menu](assets/screenshots/chalk/02-menu.png) | ![settings](assets/screenshots/chalk/03-settings.png) | ![options](assets/screenshots/chalk/04-options.png) | ![color](assets/screenshots/chalk/01-color-picker.png) |

</details>

<details>
<summary>Pebble Time / Time Steel</summary>

| Detail | List | Settings | Options | Accent Color |
| ------ | ---- | -------- | ------- | ------------ |
| ![detail](assets/screenshots/basalt/01-detail.png) | ![menu](assets/screenshots/basalt/02-menu.png) | ![settings](assets/screenshots/basalt/03-settings.png) | ![options](assets/screenshots/basalt/04-options.png) | ![color](assets/screenshots/basalt/01-color-picker.png) |

</details>

<details>
<summary>Pebble / Pebble Steel</summary>

| Detail | List | Flipped |
| ------ | ---- | ------- |
| ![detail](assets/screenshots/aplite/01-detail.png) | ![menu](assets/screenshots/aplite/02-menu.png) | ![flipped](assets/screenshots/aplite/03-flipped.png) |

</details>

## Getting Started

Requires Python — `uv` or `pipx` keep the Pebble tool off the system Python.

1. Clone: `git clone https://github.com/blakegearin/timer-betterer.git && cd timer-betterer`
2. Install the community Pebble tool, which provides `pebble` on PATH: `uv tool install pebble-tool`
3. Install an SDK, which brings its own arm toolchain and emulator: `pebble sdk install 4.33.1`
4. Build for all six platforms, into `build/`: `pebble build`
5. Run it on an emulator: `pebble install --emulator basalt` — or `aplite|basalt|chalk|diorite|emery|gabbro`

## Credits

- Fork of [pebble/pebble-timer](https://github.com/pebble/pebble-timer),
  originally by Eric Phillips for Pebble
- The settings-row icon is `Pebble_25x25_Settings.svg` from
  [pebble-dev/iconography](https://github.com/pebble-dev/iconography)
  (Apache 2.0), as are the 80×80 Timeline-pin drawings
