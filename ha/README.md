# Home Assistant integration for the Dell S718QL

`custom_components/dell_s718ql` is a Home Assistant custom integration. It
tracks the projector over PJLink (TCP 4352) and powers it on from standby with
a Wake-on-LAN magic packet. Background:
[docs/network-control.md](../docs/network-control.md).

## Projector side

Turn on the firmware option that keeps the network powered in standby. In that
state the projector answers nothing on the network except a magic packet.

## Install

1. Copy `custom_components/dell_s718ql` into `<config>/custom_components/` on
   the Home Assistant machine and restart Home Assistant.
2. Switch the projector **on** (setup checks that PJLink answers).
3. Settings → Devices & services → Add integration → "Dell S718QL Projector".
   Enter the IP address, the wired MAC address and, if needed, your subnet's
   broadcast address (the default `255.255.255.255` works when Home Assistant
   is on the same LAN segment with host networking).

Give the projector a fixed IP address (DHCP reservation).

## What you get

- **Media player** — on/off, source select, audio mute. Attributes:
  `power_state` (`on`, `warming`, `cooling`, `off`, `standby`, `waking`),
  `picture_muted`, `errors`.
- **Light source hours** sensor (keeps its last value while in standby).

Behaviour:

- No PJLink answer is treated as **standby** and shown as "off", not
  "unavailable".
- **Turn on:** PJLink power-on if the projector answers; otherwise a magic
  packet, then polling every 3 s for two minutes until it comes up.
- **Turn off:** PJLink power-off; a no-op when already in standby.
- Source names are generic ("Digital 1" … "Network") because the PJLink input
  codes have not been matched to the physical sockets yet. Edit `INPUTS` in
  `pjlink.py` once you know them.

## Tests

```sh
cd ha
uv run --python 3.13 --with pytest-homeassistant-custom-component pytest -q tests
```

They run the integration inside Home Assistant's test harness against a fake
projector. Not yet run against the real one.
