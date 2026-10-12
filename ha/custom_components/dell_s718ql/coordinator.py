"""Polling and commands for the Dell S718QL."""
from __future__ import annotations

from datetime import timedelta
import logging
import time

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_HOST, CONF_MAC, CONF_PORT
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator

from .const import (
    CONF_BROADCAST_ADDRESS, DEFAULT_BROADCAST_ADDRESS, DEFAULT_PORT, DOMAIN,
    FAST_POLL_SECONDS, FAST_POLL_WINDOW, POLL_SECONDS,
)
from .pjlink import PJLink, PJLinkError, PJLinkNoReply, read_state, send_magic_packet

_LOGGER = logging.getLogger(__name__)

type S718ConfigEntry = ConfigEntry[S718Coordinator]


class S718Coordinator(DataUpdateCoordinator[dict]):
    """Tracks the projector over PJLink.

    In standby the projector's network side only listens for Wake-on-LAN, so
    "does not answer" is reported as off rather than as unavailable.
    """

    config_entry: S718ConfigEntry

    def __init__(self, hass: HomeAssistant, entry: S718ConfigEntry) -> None:
        super().__init__(hass, _LOGGER, config_entry=entry, name=DOMAIN,
                         update_interval=timedelta(seconds=POLL_SECONDS))
        self.host: str = entry.data[CONF_HOST]
        self.port: int = entry.data.get(CONF_PORT, DEFAULT_PORT)
        self.mac: str | None = entry.data.get(CONF_MAC)
        self.broadcast: str = entry.data.get(
            CONF_BROADCAST_ADDRESS, DEFAULT_BROADCAST_ADDRESS)
        self._fast_until = 0.0
        self._waking = False

    def _link(self) -> PJLink:
        return PJLink(self.host, self.port)

    async def _async_update_data(self) -> dict:
        try:
            async with self._link() as link:
                state = await read_state(link)
            state["reachable"] = True
        except (OSError, TimeoutError, PJLinkError) as err:
            _LOGGER.debug("no answer from %s: %s", self.host, err)
            state = {"reachable": False,
                     "power": "waking" if self._waking else "standby"}
        fast = time.monotonic() < self._fast_until
        if state["reachable"] or not fast:
            self._waking = False
        self.update_interval = timedelta(
            seconds=FAST_POLL_SECONDS if fast else POLL_SECONDS)
        return state

    def _poll_fast(self) -> None:
        self._fast_until = time.monotonic() + FAST_POLL_WINDOW

    async def async_command(self, body: str, param: str) -> None:
        try:
            async with self._link() as link:
                await link.command(body, param)
        except (OSError, TimeoutError, PJLinkError) as err:
            raise HomeAssistantError(
                f"Projector did not accept {body} {param}: {err or type(err).__name__}") from err
        self._poll_fast()
        await self.async_request_refresh()

    async def async_turn_on(self) -> None:
        """PJLink power-on if it answers, otherwise a Wake-on-LAN packet."""
        try:
            async with self._link() as link:
                await link.command("POWR", "1")
        except (OSError, TimeoutError) as err:
            if not self.mac:
                raise HomeAssistantError(
                    "Projector is not answering and no MAC address is "
                    "configured for Wake-on-LAN") from err
            _LOGGER.debug("sending magic packet to %s via %s", self.mac, self.broadcast)
            await self.hass.async_add_executor_job(
                send_magic_packet, self.mac, self.broadcast)
            self._waking = True
        except PJLinkError as err:
            raise HomeAssistantError(f"Projector refused power on: {err}") from err
        self._poll_fast()
        await self.async_request_refresh()

    async def async_turn_off(self) -> None:
        if not self.data or not self.data.get("reachable"):
            return  # already in standby
        try:
            async with self._link() as link:
                await link.command("POWR", "0")
        except PJLinkNoReply as err:
            # The projector acts on power-off but drops off the network
            # without answering it; the next polls show whether it worked.
            _LOGGER.debug("no reply to power off (expected): %s", err)
        except (OSError, TimeoutError, PJLinkError) as err:
            raise HomeAssistantError(
                f"Projector did not accept power off: {err or type(err).__name__}") from err
        self._poll_fast()
        await self.async_request_refresh()
