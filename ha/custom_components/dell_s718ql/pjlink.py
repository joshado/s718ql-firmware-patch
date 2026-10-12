"""Minimal async PJLink class 1 client and Wake-on-LAN sender.

No Home Assistant imports, so it can be tested on its own. Protocol notes for
this projector are in docs/network-control.md.
"""
from __future__ import annotations

import asyncio
import hashlib
import socket

POWER_STATES = {"0": "off", "1": "on", "2": "cooling", "3": "warming"}

# The projector answers INST with these codes. PJLink only fixes the first
# digit (3 = digital, 4 = storage, 5 = network); which socket each one is has
# not been mapped on hardware yet, hence the generic names.
INPUTS = {
    "31": "Digital 1", "32": "Digital 2", "33": "Digital 3",
    "41": "Storage 1", "42": "Storage 2", "43": "Storage 3",
    "44": "Storage 4", "45": "Storage 5", "46": "Storage 6",
    "51": "Network",
}

ERST_FIELDS = ("fan", "lamp", "temperature", "cover", "filter", "other")
ERST_LEVELS = {"0": "ok", "1": "warning", "2": "error"}


class PJLinkError(Exception):
    """The projector answered, but not with what was asked for."""


class PJLinkNoReply(PJLinkError):
    """The command was sent but the projector never answered it."""


class PJLink:
    """One PJLink session: `async with PJLink(host) as link: ...`."""

    def __init__(self, host: str, port: int = 4352, password: str = "",
                 timeout: float = 4.0) -> None:
        self.host, self.port = host, port
        self.password, self.timeout = password, timeout
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._prefix = ""

    async def __aenter__(self) -> PJLink:
        self._reader, self._writer = await asyncio.wait_for(
            asyncio.open_connection(self.host, self.port), self.timeout)
        greeting = (await self._line()).split()
        if not greeting or greeting[0].upper() != "PJLINK":
            raise PJLinkError(f"unexpected greeting {greeting!r}")
        if len(greeting) >= 3 and greeting[1] == "1":
            self._prefix = hashlib.md5(
                (greeting[2] + self.password).encode()).hexdigest()
        return self

    async def __aexit__(self, *exc) -> None:
        self._writer.close()
        try:
            await asyncio.wait_for(self._writer.wait_closed(), 1)
        except (OSError, asyncio.TimeoutError):
            pass

    async def _line(self) -> str:
        try:
            raw = await asyncio.wait_for(self._reader.readuntil(b"\r"), self.timeout)
        except asyncio.TimeoutError as err:
            raise PJLinkNoReply("timed out waiting for a reply") from err
        except (asyncio.IncompleteReadError, ConnectionError) as err:
            raise PJLinkNoReply("connection closed") from err
        except asyncio.LimitOverrunError as err:
            raise PJLinkError("reply too long") from err
        return raw[:-1].decode("ascii", "replace")

    async def command(self, body: str, param: str) -> str:
        """Send %1<body> <param> and return the reply value. Raises on ERRn."""
        self._writer.write(f"{self._prefix}%1{body} {param}\r".encode("ascii"))
        self._prefix = ""  # the digest only goes with the first command
        await self._writer.drain()
        want = f"%1{body}=".upper()
        # The projector pads some replies (INF1) with stale bytes and extra
        # CRs, so skip lines that are not the reply to this command.
        for _ in range(4):
            line = await self._line()
            if "PJLINK ERRA" in line.upper():
                raise PJLinkError("authentication failed")
            at = line.upper().rfind(want)
            if at < 0:
                continue
            value = line[at + len(want):]
            if value.upper().startswith("ERR"):
                raise PJLinkError(f"{body} {param}: {value}")
            return value
        raise PJLinkNoReply(f"no reply to {body}")

    async def query(self, body: str) -> str:
        return await self.command(body, "?")


async def read_state(link: PJLink) -> dict:
    """Poll what class 1 offers. Fields the projector refuses come back None."""
    async def q(body: str) -> str | None:
        try:
            return await link.query(body)
        except PJLinkError:
            return None

    state: dict = {"power": POWER_STATES.get(await link.query("POWR"), "unknown")}
    state["input"] = await q("INPT")
    avmt = await q("AVMT")
    state["video_mute"] = None if avmt is None else avmt in ("11", "31")
    state["audio_mute"] = None if avmt is None else avmt in ("21", "31")
    lamp = await q("LAMP")
    try:
        state["light_hours"] = int(lamp.split()[0]) if lamp else None
    except ValueError:
        state["light_hours"] = None
    erst = await q("ERST")
    state["errors"] = (
        {f: ERST_LEVELS.get(c, c) for f, c in zip(ERST_FIELDS, erst)}
        if erst and len(erst) == 6 else None)
    return state


def send_magic_packet(mac: str, broadcast: str = "255.255.255.255",
                      repeat: int = 3) -> None:
    """Send a Wake-on-LAN magic packet (UDP ports 9 and 7, broadcast)."""
    raw = bytes.fromhex(mac.replace(":", "").replace("-", ""))
    if len(raw) != 6:
        raise ValueError(f"bad MAC address {mac!r}")
    packet = b"\xff" * 6 + raw * 16
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        for _ in range(repeat):
            for port in (9, 7):
                sock.sendto(packet, (broadcast, port))
