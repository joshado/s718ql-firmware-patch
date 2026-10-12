"""Config flow: host, MAC for Wake-on-LAN, broadcast address."""
from __future__ import annotations

import voluptuous as vol

from homeassistant.config_entries import ConfigFlow, ConfigFlowResult
from homeassistant.const import CONF_HOST, CONF_MAC, CONF_NAME
from homeassistant.helpers.device_registry import format_mac

from .const import CONF_BROADCAST_ADDRESS, DEFAULT_BROADCAST_ADDRESS, DOMAIN
from .pjlink import PJLink, PJLinkError

SCHEMA = vol.Schema({
    vol.Required(CONF_NAME, default="Projector"): str,
    vol.Required(CONF_HOST): str,
    vol.Required(CONF_MAC): str,
    vol.Required(CONF_BROADCAST_ADDRESS, default=DEFAULT_BROADCAST_ADDRESS): str,
})


class S718ConfigFlow(ConfigFlow, domain=DOMAIN):
    VERSION = 1

    async def async_step_user(self, user_input: dict | None = None) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            mac = format_mac(user_input[CONF_MAC])
            if len(mac) != 17:
                errors[CONF_MAC] = "invalid_mac"
            else:
                await self.async_set_unique_id(mac)
                self._abort_if_unique_id_configured()
                # The projector has to be on for this: in standby it only
                # listens for Wake-on-LAN.
                try:
                    async with PJLink(user_input[CONF_HOST]) as link:
                        await link.query("POWR")
                except (OSError, TimeoutError, PJLinkError):
                    errors["base"] = "cannot_connect"
                else:
                    return self.async_create_entry(
                        title=user_input[CONF_NAME],
                        data={CONF_HOST: user_input[CONF_HOST], CONF_MAC: mac,
                              CONF_BROADCAST_ADDRESS: user_input[CONF_BROADCAST_ADDRESS]})
        return self.async_show_form(
            step_id="user",
            data_schema=self.add_suggested_values_to_schema(SCHEMA, user_input or {}),
            errors=errors)
