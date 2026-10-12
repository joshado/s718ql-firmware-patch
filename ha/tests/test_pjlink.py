import pytest

from custom_components.dell_s718ql.pjlink import PJLink, read_state, send_magic_packet


async def test_state_and_junk_after_reply(projector):
    async with PJLink("127.0.0.1", projector.port, timeout=1) as link:
        assert await link.query("INF1") == "Dell"
        # the junk the projector sends after INF1 must not derail what follows
        assert await link.query("NAME") == "DELL S718QL"
        state = await read_state(link)
    assert state == {"power": "on", "input": "31", "video_mute": False,
                     "audio_mute": False, "light_hours": 41,
                     "errors": dict.fromkeys(
                         ("fan", "lamp", "temperature", "cover", "filter", "other"), "ok")}


async def test_commands(projector):
    async with PJLink("127.0.0.1", projector.port, timeout=1) as link:
        await link.command("INPT", "32")
        await link.command("POWR", "0")
    assert (projector.inpt, projector.power) == ("32", "0")


def test_magic_packet(monkeypatch):
    sent = []

    class Sock:
        def __init__(self, *a): pass
        def __enter__(self): return self
        def __exit__(self, *a): pass
        def setsockopt(self, *a): pass
        def sendto(self, data, addr): sent.append((data, addr))

    monkeypatch.setattr("socket.socket", Sock)
    send_magic_packet("aa:bb:cc:dd:ee:ff", "192.0.2.255", repeat=1)
    mac = bytes.fromhex("aabbccddeeff")
    assert sent == [(b"\xff" * 6 + mac * 16, ("192.0.2.255", 9)),
                    (b"\xff" * 6 + mac * 16, ("192.0.2.255", 7))]
    with pytest.raises(ValueError):
        send_magic_packet("nope")
