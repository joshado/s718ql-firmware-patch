"""The projector as a media player: power, input, audio mute."""
from __future__ import annotations

from homeassistant.components.media_player import (
    MediaPlayerEntity, MediaPlayerEntityFeature, MediaPlayerState,
)
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .coordinator import S718ConfigEntry, S718Coordinator
from .entity import S718Entity
from .pjlink import INPUTS

SOURCE_TO_CODE = {name: code for code, name in INPUTS.items()}


async def async_setup_entry(hass: HomeAssistant, entry: S718ConfigEntry,
                            async_add_entities: AddConfigEntryEntitiesCallback) -> None:
    async_add_entities([S718MediaPlayer(entry.runtime_data)])


class S718MediaPlayer(S718Entity, MediaPlayerEntity):
    _attr_name = None
    _attr_icon = "mdi:projector"
    _attr_source_list = list(INPUTS.values())
    _attr_supported_features = (
        MediaPlayerEntityFeature.TURN_ON
        | MediaPlayerEntityFeature.TURN_OFF
        | MediaPlayerEntityFeature.SELECT_SOURCE
        | MediaPlayerEntityFeature.VOLUME_MUTE
    )

    def __init__(self, coordinator: S718Coordinator) -> None:
        super().__init__(coordinator, "media_player")

    # No answer means standby (only Wake-on-LAN is listening), so the entity
    # stays available and shows "off" instead of going unavailable.
    @property
    def available(self) -> bool:
        return self.coordinator.data is not None

    @property
    def state(self) -> MediaPlayerState:
        power = self.coordinator.data["power"]
        return MediaPlayerState.ON if power in ("on", "warming") else MediaPlayerState.OFF

    @property
    def source(self) -> str | None:
        code = self.coordinator.data.get("input")
        return INPUTS.get(code, code)

    @property
    def is_volume_muted(self) -> bool | None:
        return self.coordinator.data.get("audio_mute")

    @property
    def extra_state_attributes(self) -> dict:
        data = self.coordinator.data
        attrs = {"power_state": data["power"]}
        if data.get("video_mute") is not None:
            attrs["picture_muted"] = data["video_mute"]
        if data.get("errors"):
            attrs["errors"] = data["errors"]
        return attrs

    async def async_turn_on(self) -> None:
        await self.coordinator.async_turn_on()

    async def async_turn_off(self) -> None:
        await self.coordinator.async_turn_off()

    async def async_select_source(self, source: str) -> None:
        code = SOURCE_TO_CODE.get(source)
        if code is None:
            raise HomeAssistantError(f"Unknown source {source!r}")
        await self.coordinator.async_command("INPT", code)

    async def async_mute_volume(self, mute: bool) -> None:
        await self.coordinator.async_command("AVMT", "21" if mute else "20")
