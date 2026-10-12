#!/usr/bin/env python3
"""Flash the NXP LPC11U67 application over RS232, speaking the BootCode's
block protocol (what Dell's PIC_NXP_Downloader.exe does). See
docs/nxp-mcu.md, "Serial download protocol".

UNTESTED ON HARDWARE. The protocol was read out of the downloader and the
BootCode; this script has only been run against the simulator below.

usage: nxp_flash.py IMAGE                     show what would be sent
       nxp_flash.py IMAGE --selftest          run against a simulated BootCode
       nxp_flash.py IMAGE --port DEV --check  cable test, writes nothing (see below)
       nxp_flash.py IMAGE --port DEV --flash  really flash (needs pyserial)

IMAGE is an Intel HEX file, or a flat binary whose offset 0 is flash address 0.
Put the projector in download mode first: hold Power, apply AC, release when
the Power LED blinks white (4-6 s). Start this script only after that.

--check is for a projector in ordinary standby (NOT download mode): it sends
the application's own "#00501 1" query (WoL flag) and expects "P0" or "P1"
back. A reply proves the adapter, cable wiring and baud rate before anything
is erased.
"""
import argparse
import sys
import time
from pathlib import Path

APP_BASE = 0x2000
FLASH_END = 0x20000
BLOCK = 1024
CONFIG_ADDR = 0x1FF00
DEVICE_LPC11U67 = 0x14
CMD_RUN, ACK, CMD_WRITE, CMD_RESTART = 0x00, 0x01, 0x03, 0xCC
DEVICES = {0x02: "PIC24FJ32GA002", 0x06: "PIC24FJ64GB106",
           0x13: "LPC1113-303", 0x14: "LPC11U67JBD100"}


def load_image(path):
    """Return a flat 0xFF-filled image, length rounded up to a whole block."""
    raw = path.read_bytes()
    if not raw.lstrip().startswith(b":"):
        mem = {i: b for i, b in enumerate(raw)}
    else:
        mem, upper = {}, 0
        for n, line in enumerate(raw.decode("ascii").split(), 1):
            if not line.startswith(":"):
                raise ValueError(f"line {n}: not an Intel HEX record")
            rec = bytes.fromhex(line[1:])
            if sum(rec) & 0xFF:
                raise ValueError(f"line {n}: bad record checksum")
            count, addr, kind, data = rec[0], rec[1] << 8 | rec[2], rec[3], rec[4:-1]
            if len(data) != count:
                raise ValueError(f"line {n}: length mismatch")
            if kind == 0:
                for i, b in enumerate(data):
                    mem[(upper << 16) + addr + i] = b
            elif kind == 4:
                upper = data[0] << 8 | data[1]
            elif kind == 2:
                raise ValueError(f"line {n}: segment records not supported")
    end = max(mem) + 1
    size = -(-end // BLOCK) * BLOCK
    image = bytearray(b"\xff" * size)
    for a, b in mem.items():
        image[a] = b
    return bytes(image)


def check_image(image):
    """Refuse images the BootCode would not boot, or that we should not send."""
    problems = []
    if len(image) > FLASH_END:
        problems.append(f"image extends to {len(image):#x}, past flash end {FLASH_END:#x}")
    if len(image) < CONFIG_ADDR + 3:
        problems.append("image stops before the config word at 0x1ff00")
    else:
        if image[CONFIG_ADDR] != 0xAA:
            problems.append("config byte at 0x1ff00 is not 0xAA: BootCode would "
                            "erase the app on next boot")
    if len(image) >= APP_BASE + 8:
        sp = int.from_bytes(image[APP_BASE:APP_BASE + 4], "little")
        reset = int.from_bytes(image[APP_BASE + 4:APP_BASE + 8], "little")
        if not 0x10000000 <= sp <= 0x10009000:
            problems.append(f"initial SP {sp:#x} is not in RAM")
        # BootCode test at 0xe90: (reset - 0x2000) < 0x10001, else it blinks forever
        if not 0 <= reset - APP_BASE < 0x10001 or not reset & 1:
            problems.append(f"reset vector {reset:#x} fails the BootCode range check")
    else:
        problems.append("image has no application region")
    return problems


def blocks(image):
    end = min(len(image), FLASH_END)
    return [(a, image[a:a + BLOCK]) for a in range(APP_BASE, end, BLOCK)]


class SimBootCode:
    """The BootCode's download loop (0x6fe-0x7de) as a byte-level simulator."""

    def __init__(self):
        self.flash = bytearray(b"\xff" * FLASH_END)
        self.ptr = APP_BASE
        self.rx = bytearray()
        self.tx = bytearray()
        self.synced = False
        self.running_app = False

    def write(self, data):
        self.rx += data
        while self._step():
            pass

    def _sync(self):
        if len(self.rx) < 2:
            return False
        if bytes(self.rx[:2]) != b"\xaa\x55":
            raise RuntimeError("sim: bad sync bytes, BootCode would reset")
        del self.rx[:2]
        self.tx.append(DEVICE_LPC11U67)
        return True

    def _step(self):
        if not self.rx or self.running_app:
            return False
        if not self.synced:
            self.synced = self._sync()
            return self.synced
        cmd = self.rx[0]
        if cmd == CMD_WRITE:
            if len(self.rx) < 1 + BLOCK:
                return False
            data = bytes(self.rx[1:1 + BLOCK])
            del self.rx[:1 + BLOCK]
            if self.ptr == APP_BASE:
                self.flash[APP_BASE:] = b"\xff" * (FLASH_END - APP_BASE)
            ok = self.ptr + BLOCK <= FLASH_END
            if ok:
                self.flash[self.ptr:self.ptr + BLOCK] = data
            self.ptr += BLOCK
            self.tx.append(ACK if ok else 0)
        elif cmd == CMD_RUN:
            del self.rx[:1]
            self.running_app = True
        elif cmd == CMD_RESTART:
            del self.rx[:1]
            self.ptr = APP_BASE
            self.synced = False
        else:
            del self.rx[:1]
            self.tx.append(0)
        return True

    def read(self, n):
        out = bytes(self.tx[:n])
        del self.tx[:n]
        return out

    def reset_input_buffer(self):
        self.tx.clear()


def read_byte(port, what):
    b = port.read(1)
    if len(b) != 1:
        raise RuntimeError(f"timeout waiting for {what}")
    return b[0]


def flash(port, image, progress=True):
    todo = blocks(image)
    port.reset_input_buffer()
    port.write(b"\xaa\x55")
    dev = read_byte(port, "device id")
    if dev != DEVICE_LPC11U67:
        raise RuntimeError(f"device id {dev:#04x} ({DEVICES.get(dev, 'unknown')}), "
                           f"expected {DEVICE_LPC11U67:#04x}; nothing written")
    for i, (addr, data) in enumerate(todo, 1):
        port.write(bytes([CMD_WRITE]))
        port.write(data)
        ack = read_byte(port, f"ack for block at {addr:#x}")
        if ack != ACK:
            raise RuntimeError(f"block at {addr:#x} rejected (reply {ack:#04x}). The app "
                               "area is now incomplete; re-enter download mode and retry")
        if progress:
            print(f"\r  {i}/{len(todo)} blocks, up to {addr + BLOCK:#x}", end="", flush=True)
    if progress:
        print()
    port.write(bytes([CMD_RUN]))


def check_link(port):
    port.reset_input_buffer()
    port.write(b"#00501 1\r")
    reply = port.read(3)
    if reply in (b"P0\r", b"P1\r"):
        print(f"link ok: MCU answered {reply!r} (WoL flag = {chr(reply[1])})")
        return 0
    print(f"no valid answer (got {reply!r}). Check the port, try a null-modem "
          "adapter, and make sure the projector is in standby, not download mode.")
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("image", type=Path)
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=19200)
    ap.add_argument("--flash", action="store_true", help="really write to the device")
    ap.add_argument("--check", action="store_true",
                    help="cable test against a projector in standby; writes nothing")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--force", action="store_true", help="ignore image sanity checks")
    args = ap.parse_args()

    image = load_image(args.image)
    todo = blocks(image)
    print(f"image: {len(image):#x} bytes; {len(todo)} blocks of {BLOCK} from "
          f"{APP_BASE:#x} to {APP_BASE + len(todo) * BLOCK:#x}")
    problems = check_image(image)
    for p in problems:
        print("PROBLEM:", p)
    if problems and not args.force:
        return 1

    if args.selftest:
        sim = SimBootCode()
        flash(sim, image, progress=False)
        end = APP_BASE + len(todo) * BLOCK
        ok = sim.flash[APP_BASE:end] == image[APP_BASE:end] and sim.running_app
        print("selftest:", "ok" if ok else "MISMATCH")
        return 0 if ok else 1

    if args.check:
        if not args.port:
            ap.error("--check needs --port")
        import serial  # pyserial
        with serial.Serial(args.port, args.baud, bytesize=8, parity="N", stopbits=1,
                           timeout=2) as port:
            return check_link(port)

    if not args.flash:
        print("dry run: nothing sent. Add --port DEV --flash to write.")
        return 0
    if not args.port:
        ap.error("--flash needs --port")
    import serial  # pyserial
    with serial.Serial(args.port, args.baud, bytesize=8, parity="N", stopbits=1,
                       timeout=10) as port:
        start = time.time()
        flash(port, image)
        print(f"done in {time.time() - start:.0f} s; BootCode told to start the app")
    return 0


if __name__ == "__main__":
    sys.exit(main())
