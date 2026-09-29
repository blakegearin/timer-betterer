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

| List | Timer | Settings | Accent Color |
| ---- | ----- | -------- | ------------ |
| <img src="assets/renders/emery/01-list.png" alt="list" width="146"> | <img src="assets/renders/emery/02-detail.png" alt="timer" width="146"> | <img src="assets/renders/emery/03-settings.png" alt="settings" width="146"> | <img src="assets/renders/emery/04-color.png" alt="color" width="146"> |

</details>

<details>
<summary>Pebble 2 Duo</summary>

| List | Timer | Settings | Accent Color |
| ---- | ----- | -------- | ------------ |
| <img src="assets/renders/gabbro/01-list.png" alt="list" width="165"> | <img src="assets/renders/gabbro/02-detail.png" alt="timer" width="165"> | <img src="assets/renders/gabbro/03-settings.png" alt="settings" width="165"> | <img src="assets/renders/gabbro/04-color.png" alt="color" width="165"> |

</details>

<details>
<summary>Pebble 2</summary>

| List | Timer | Settings |
| ---- | ----- | -------- |
| <img src="assets/renders/diorite/01-list.png" alt="list" width="110"> | <img src="assets/renders/diorite/02-detail.png" alt="timer" width="110"> | <img src="assets/renders/diorite/03-settings.png" alt="settings" width="110"> |

</details>

<details>
<summary>Pebble Time Round</summary>

| List | Timer | Settings | Accent Color |
| ---- | ----- | -------- | ------------ |
| <img src="assets/renders/chalk/01-list.png" alt="list" width="145"> | <img src="assets/renders/chalk/02-detail.png" alt="timer" width="145"> | <img src="assets/renders/chalk/03-settings.png" alt="settings" width="145"> | <img src="assets/renders/chalk/04-color.png" alt="color" width="145"> |

</details>

<details>
<summary>Pebble Time / Time Steel</summary>

| List | Timer | Settings | Accent Color |
| ---- | ----- | -------- | ------------ |
| <img src="assets/renders/basalt/01-list.png" alt="list" width="134"> | <img src="assets/renders/basalt/02-detail.png" alt="timer" width="134"> | <img src="assets/renders/basalt/03-settings.png" alt="settings" width="134"> | <img src="assets/renders/basalt/04-color.png" alt="color" width="134"> |

</details>

<details>
<summary>Pebble / Pebble Steel</summary>

| List | Timer | Settings |
| ---- | ----- | -------- |
| <img src="assets/renders/aplite/01-list.png" alt="list" width="123"> | <img src="assets/renders/aplite/02-detail.png" alt="timer" width="123"> | <img src="assets/renders/aplite/03-settings.png" alt="settings" width="123"> |

</details>

## Getting Started

Requires Python — `uv` or `pipx` keep the Pebble tool off the system Python.

1. Clone: `git clone https://github.com/blakegearin/timer-betterer.git && cd timer-betterer`
2. Install the community Pebble tool, which provides `pebble` on PATH: `uv tool install pebble-tool`
3. Install an SDK, which brings its own arm toolchain and emulator: `pebble sdk install 4.33.1`
4. Build for all six platforms, into `build/`: `pebble build`
5. Run it on an emulator: `pebble install --emulator basalt` — or `aplite|basalt|chalk|diorite|emery|gabbro`

## Credits

- Fork of [coredevices/pebble-timer](https://github.com/coredevices/pebble-timer),
  originally by Eric Phillips for Pebble
- The settings-row icon is `Pebble_25x25_Settings.svg` from
  [pebble-dev/iconography](https://github.com/pebble-dev/iconography)
  (Apache 2.0), as are the 80×80 Timeline-pin drawings
- The device artwork in `tools/renders/svg/` is official Pebble press art
  from [developer.repebble.com](https://developer.repebble.com); the
  screen geometry in `tools/renders.sh` mirrors the developer site's own
  `.pebble-screenshot` stylesheet
