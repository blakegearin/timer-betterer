#!/usr/bin/env python3
"""fasttick: pin the emulator clock, press a button, and capture a frame --
all inside one websocket session.

Why this exists: every `pebble` CLI call takes about a second, and the
firmware keeps a time pin only until pypkjs re-syncs the clock to the
phone's -- the host's -- wall time, a couple of seconds later. A running
countdown started inside a pin's island therefore expires (Time's Up!) on
any tick that catches a leaked real time, and a slow CLI chain always
loses that race. This script connects once, pins time A, clicks, then
re-pins the capture time every 100 ms -- each pin resets pypkjs' re-sync
deadline, so the island never dies while the frame is taken -- then hands
the screenshot back over the same connection.

Run it with the pebble tool's own python (the `pebble` script's shebang);
that's the interpreter holding libpebble2, pypkjs' protocol classes and PIL.

Usage: fasttick.py <platform> <timeA HH:MM:SS> <button> <timeF HH:MM:SS> <out.png> [hold_seconds]

The frame's countdown reading: a timer of duration D started at timeA shows
(timeA + D - timeF) seconds when captured on the timeF island, minus up to
one tick of redraw latency -- pick timeA so that lands on the number you
want to see (see tools/scenes/readme-tour.scene).
"""
import datetime
import os
import sys
import time as systime

from pebble_tool.sdk.emulator import ManagedEmulatorTransport
from libpebble2.communication import PebbleConnection
from libpebble2.protocol.system import TimeMessage, SetUTC
from libpebble2.communication.transports.websocket import MessageTargetPhone
from libpebble2.communication.transports.qemu.protocol import QemuButton
from libpebble2.services.screenshot import Screenshot
from pebble_tool.commands.emucontrol import send_data_to_qemu

BUTTONS = {
    "back": QemuButton.Button.Back,
    "up": QemuButton.Button.Up,
    "select": QemuButton.Button.Select,
    "down": QemuButton.Button.Down,
}


def epoch_today(hhmmss):
    hour, minute, second = (int(p) for p in hhmmss.split(":"))
    now = datetime.datetime.now()
    dt = datetime.datetime(now.year, now.month, now.day, hour, minute, second)
    return int(dt.timestamp())


def tz_minutes():
    is_dst = systime.localtime().tm_isdst and systime.daylight
    return int((-systime.altzone if is_dst else -systime.timezone) // 60)


def main():
    plat, time_a, button, time_f, out = sys.argv[1:6]
    hold = float(sys.argv[6]) if len(sys.argv) > 6 else 1.1

    transport = ManagedEmulatorTransport(plat)
    pebble = PebbleConnection(transport)
    pebble.connect()
    pebble.run_async()

    if button not in BUTTONS:
        raise SystemExit("unknown button " + button)

    def pin(ts):
        pebble.send_packet(TimeMessage(message=SetUTC(
            unix_time=ts, utc_offset=tz_minutes(),
            tz_name="UTC{:+d}".format(tz_minutes() // 60))))

    ts_a, ts_f = epoch_today(time_a), epoch_today(time_f)

    pin(ts_a)
    systime.sleep(0.15)          # the pin lands before the click's now= is read
    send_data_to_qemu(pebble.transport, QemuButton(state=BUTTONS[button]))
    systime.sleep(0.08)
    send_data_to_qemu(pebble.transport, QemuButton(state=0))

    # Hold the island: re-pin every 100ms so pypkjs' re-sync deadline slides
    # forward forever, and give the app one full redraw tick at the held time.
    started = systime.monotonic()
    while systime.monotonic() - started < hold:
        pin(ts_f)
        systime.sleep(0.1)
    pin(ts_f)
    screenshot = Screenshot(pebble)
    rows = screenshot.grab_image()
    # Same post-processing as `pebble screenshot`: raw 6-bit triples -> the
    # device palette, then the round-platform corner mask.
    from pebble_tool.commands.screenshot import ScreenshotCommand
    import png
    sc = ScreenshotCommand.__new__(ScreenshotCommand)
    sc.pebble = pebble
    png.from_array(sc._roundify(sc._correct_colours(rows)), mode="RGBA;8").save(out)
    print("fasttick: " + out, flush=True)
    # run_async leaves its pump threads in place; exit directly.
    os._exit(0)


if __name__ == "__main__":
    main()
