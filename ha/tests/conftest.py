import asyncio
import pathlib
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent.parent))


class FakeProjector:
    """PJLink server that mimics the S718QL: lower-case replies, junk after INF1."""

    def __init__(self):
        self.power, self.inpt, self.avmt = "1", "31", "30"
        self.silent_power_off = False
        self.server = None
        self.port = None

    async def start(self, port=0):
        self.server = await asyncio.start_server(self._client, "127.0.0.1", port)
        self.port = self.server.sockets[0].getsockname()[1]

    async def stop(self):
        if self.server:
            self.server.close()
            await self.server.wait_closed()
            self.server = None

    async def _client(self, reader, writer):
        writer.write(b"pjlink 0\r")
        try:
            while True:
                line = (await reader.readuntil(b"\r"))[:-1].decode()
                body, param = line[2:6].lower(), line[7:]
                if self.silent_power_off and (body, param) == ("powr", "0"):
                    self.power = "0"        # acts on it, never answers
                    await asyncio.sleep(1)
                tail = b"\r\x00718QL\r\x00qI\x07," if body == "inf1" else b"\r"
                writer.write(f"%1{body}={self._reply(body, param)}".encode() + tail)
                await writer.drain()
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            writer.close()

    def _reply(self, body, param):
        if body == "powr":
            if param == "?":
                return self.power
            self.power = param
            return "OK"
        if body == "inpt":
            if param == "?":
                return self.inpt
            self.inpt = param
            return "OK"
        if body == "avmt":
            if param == "?":
                return self.avmt
            self.avmt = param
            return "OK"
        return {"lamp": "41", "erst": "000000", "inf1": "Dell",
                "name": "DELL S718QL"}.get(body, "ERR1")


@pytest.fixture
async def projector(socket_enabled):
    proj = FakeProjector()
    await proj.start()
    yield proj
    await proj.stop()
