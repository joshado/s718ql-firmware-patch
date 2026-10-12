"""Shared entity base."""
from __future__ import annotations

from homeassistant.helpers.device_registry import CONNECTION_NETWORK_MAC, DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .coordinator import S718Coordinator


class S718Entity(CoordinatorEntity[S718Coordinator]):
    _attr_has_entity_name = True

    def __init__(self, coordinator: S718Coordinator, key: str) -> None:
        super().__init__(coordinator)
        entry = coordinator.config_entry
        base = entry.unique_id or entry.entry_id
        self._attr_unique_id = f"{base}_{key}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, base)},
            connections=({(CONNECTION_NETWORK_MAC, coordinator.mac)}
                         if coordinator.mac else set()),
            manufacturer="Dell",
            model="S718QL",
            name=entry.title,
        )
