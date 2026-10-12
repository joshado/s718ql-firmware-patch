from unittest.mock import patch

import pytest
from pytest_homeassistant_custom_component.common import MockConfigEntry

from homeassistant.const import CONF_HOST, CONF_MAC, CONF_PORT

from custom_components.dell_s718ql.const import DOMAIN
from custom_components.dell_s718ql.pjlink import PJLink

MAC = "aa:bb:cc:dd:ee:ff"
ENTITY = "media_player.projector"


@pytest.fixture(autouse=True)
def _enable(enable_custom_integrations):
    yield


async def setup(hass, projector):
    entry = MockConfigEntry(domain=DOMAIN, title="Projector", unique_id=MAC,
                            data={CONF_HOST: "127.0.0.1", CONF_PORT: projector.port,
                                  CONF_MAC: MAC})
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    return entry


async def call(hass, service, **data):
    await hass.services.async_call("media_player", service,
                                   {"entity_id": ENTITY, **data}, blocking=True)
    await hass.async_block_till_done()


async def test_on_state_and_commands(hass, projector):
    entry = await setup(hass, projector)
    state = hass.states.get(ENTITY)
    assert state.state == "on"
    assert state.attributes["source"] == "Digital 1"
    assert state.attributes["is_volume_muted"] is False
    assert hass.states.get("sensor.projector_light_source_hours").state == "41"

    await call(hass, "select_source", source="Digital 2")
    assert projector.inpt == "32"
    await call(hass, "volume_mute", is_volume_muted=True)
    assert projector.avmt == "21"
    await call(hass, "turn_off")
    assert projector.power == "0"
    await entry.runtime_data.async_refresh()
    await hass.async_block_till_done()
    assert hass.states.get(ENTITY).state == "off"
    assert await hass.config_entries.async_unload(entry.entry_id)


async def test_standby_wakes_with_magic_packet(hass, projector):
    port = projector.port
    await projector.stop()                    # standby: nothing answers
    entry = await setup(hass, projector)
    state = hass.states.get(ENTITY)
    assert state.state == "off"
    assert state.attributes["power_state"] == "standby"

    with patch("custom_components.dell_s718ql.coordinator.send_magic_packet") as wol:
        await call(hass, "turn_on")
    wol.assert_called_once_with(MAC, "255.255.255.255")
    assert hass.states.get(ENTITY).attributes["power_state"] == "waking"

    await projector.start(port)               # it woke up
    await entry.runtime_data.async_refresh()
    await hass.async_block_till_done()
    assert hass.states.get(ENTITY).state == "on"

    with patch("custom_components.dell_s718ql.coordinator.send_magic_packet") as wol:
        await call(hass, "turn_on")           # answers now: PJLink, no packet
    wol.assert_not_called()
    assert await hass.config_entries.async_unload(entry.entry_id)


async def test_config_flow(hass, projector):
    with patch("custom_components.dell_s718ql.config_flow.PJLink.__init__",
               lambda self, host: setattr(self, "_x", None)), \
         patch("custom_components.dell_s718ql.config_flow.PJLink.__aenter__",
               side_effect=OSError), \
         patch("custom_components.dell_s718ql.config_flow.PJLink.__aexit__", return_value=None):
        result = await hass.config_entries.flow.async_init(DOMAIN, context={"source": "user"})
        result = await hass.config_entries.flow.async_configure(
            result["flow_id"], {"name": "Projector", "host": "192.0.2.1", "mac": MAC,
                                "broadcast_address": "255.255.255.255"})
    assert result["errors"] == {"base": "cannot_connect"}
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], {"name": "Projector", "host": "192.0.2.1", "mac": "zz",
                            "broadcast_address": "255.255.255.255"})
    assert result["errors"] == {"mac": "invalid_mac"}


async def test_power_off_without_reply_is_not_an_error(hass, projector):
    """The real projector powers off but never answers POWR 0."""
    entry = await setup(hass, projector)
    projector.silent_power_off = True
    with patch("custom_components.dell_s718ql.coordinator.PJLink",
               lambda host, port: PJLink(host, port, timeout=0.3)):
        await call(hass, "turn_off")
    assert projector.power == "0"
    assert await hass.config_entries.async_unload(entry.entry_id)
