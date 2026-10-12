"""Constants for the Dell S718QL integration."""

DOMAIN = "dell_s718ql"

CONF_BROADCAST_ADDRESS = "broadcast_address"

DEFAULT_PORT = 4352
DEFAULT_BROADCAST_ADDRESS = "255.255.255.255"

POLL_SECONDS = 10
# After a wake packet or a power command, poll faster for a while.
FAST_POLL_SECONDS = 3
FAST_POLL_WINDOW = 120
