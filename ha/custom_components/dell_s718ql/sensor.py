"""Light-source hours."""
from __future__ import annotations

from homeassistant.components.sensor import (
    SensorDeviceClass, SensorEntity, SensorStateClass,
)
from homeassistant.const import EntityCategory, UnitOfTime
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .coordinator import S718ConfigEntry, S718Coordinator
from .entity import S718Entity


async def async_setup_entry(hass: HomeAssistant, entry: S718ConfigEntry,
                            async_add_entities: AddConfigEntryEntitiesCallback) -> None:
    async_add_entities([S718LightHours(entry.runtime_data)])


class S718LightHours(S718Entity, SensorEntity):
    _attr_translation_key = "light_hours"
    _attr_device_class = SensorDeviceClass.DURATION
    _attr_native_unit_of_measurement = UnitOfTime.HOURS
    _attr_state_class = SensorStateClass.TOTAL_INCREASING
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, coordinator: S718Coordinator) -> None:
        super().__init__(coordinator, "light_hours")
        self._hours: int | None = None

    # Only readable while the projector is on; keep the last value in standby.
    @property
    def available(self) -> bool:
        return self.native_value is not None

    @property
    def native_value(self) -> int | None:
        hours = (self.coordinator.data or {}).get("light_hours")
        if hours is not None:
            self._hours = hours
        return self._hours
