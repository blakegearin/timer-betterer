# TimerBetterer

A countdown timer for Pebble smartwatches, forked from Pebble's `pebble-timer` app.

## Features

- Choose your own accent color
- Configure list sorting, grouping, and wrapping
- Pause or start a timer on create
- Extra confirmation to avoid accidental deletions
- Customize snooze duration or replay timer on completion
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

1. Clone: `git clone https://github.com/blakegearin/timer-betterer.git`
2. Open: `cd timer-betterer`
3. Install [`uv`](https://docs.astral.sh/uv/getting-started/installation/) or [`pipx`](https://pipx.pypa.io/latest/how-to/install-pipx.html)
4. Install pebble-tool
   - uv: `uv tool install pebble-tool`
   - pipx: `pipx install pebble-tool`
5. Install SDK: `pebble sdk install latest`

## Running

### On Real Hardware

1. Open the Pebble app
2. Navigate to the Devices tab
3. Select the kebab icon to open the device menu
4. Enable toggle for "Dev Connection"
5. Build: `make build`
6. Install: `make sideload`

### Emulator

- Start: `make run PLAT=<platform>`

  - Pebble / Pebble Steel: `aplite`
  - Pebble Time / Pebble Time Steel: `basalt`
  - Pebble Time Round: `chalk`
  - Pebble 2: `diorite`
  - Pebble Time 2: `emery`
  - Pebble 2 Duo: `gabbro`

- Stop: `make kill`

Control the emulator with a keyboard or mouse.

- Up button: `Up`
- Down button: `Down`
- Select button: `Enter`
- Back button: `Delete` / `Backspace`
- Tapping: mouse
  - Only on touch-capable platforms: `chalk`, `emery`, `gabbro`

## Credits

- Fork of [coredevices/pebble-timer](https://github.com/coredevices/pebble-timer), originally by Eric Phillips for Pebble
- Icons from [pebble-dev/iconography](https://github.com/pebble-dev/iconography)
- Device renders from [developer.repebble.com](https://developer.repebble.com)
