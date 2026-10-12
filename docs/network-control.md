# Network control — what the AM8250 exposes, and the Home Assistant bridge

**Status (2026-10-12):** mostly static analysis; section 1a is from the live
projector.
Tags: **[code]** read from the binaries/files in
`firmware/unpacked/network/`; **[inferred]** not directly checked.

## 1. Who does what on the network board

`/etc/init.d/rcS` starts `manager.app` (→ `fui.app`, the EZCast UI) and then
`/etc/user.sh`, which starts the **ATOP** network-control module:
`sbin/set_eth`, `sbin/atop_prod_ap`, `/mnt/atop/atop_main`, `sbin/gwd`.
**[code]**

`atop_main` (MIPS, stripped, 915 KB) is the projector-control server. It talks
to the rest of the projector over `/dev/uartcom` and starts one thread per
protocol (`create PJLink/snmp/crestron/amx/web thread`): **[code]**

| Service | Port | Notes |
|---|---|---|
| **PJLink class 1** | TCP 4352 | greeting `PJLINK 0` (no authentication). Handles `POWR INPT AVMT ERST LAMP INST NAME INF1 INF2 INFO CLSS`. `INST` = `31 32 33 41 42 43 44 45 46 51`; `INF1` = `Dell`. |
| Dell web UI | HTTP (GoAhead `webs`, port from `SERVER_PORT`, presumably 80) | pages in `/mnt/atop/web` (`login.asp`, `home.asp`, `status.asp`, …), login required. A "console" form handler takes `power`, `eco`, `freeze_on/off`, `hide_on/off`, `resync`, source, display mode, `bri_m/p`, `contrast_m/p`, aspect, `pjtion`, `colortemp`, `gamma`, `keystone`, `vgaout`, `autodown`, `vol_m/p`, `mute`, and returns status as `{power:"%d",eco:"%d",display:…,vol:"%d",mute:"%d",…}`. |
| AMX | TCP 4661, UDP beacon to 239.255.250.250 | ASCII commands `#0000` power, `#0001` remote keys, `#0002` resync, `#0003` AV mute, `#0004` freeze, `#0005` source, `#0010`–`#0013` colour mode / brightness / contrast / sharpness, `#0020`–`#0023` aspect / zoom / keystone / orientation, `#0030`–`#0032` language / menu location / reset, `#0040`–`#0042` mute / volume / mic volume, `#0050` auto power off, `#0051` high altitude. Wire framing not worked out. |
| Crestron | TCP 41794 (control), 843 (Flash UI), optional outbound to a control box | |
| SNMP | (UDP 161 **[inferred]**) | |
| e-mail alerts, weekly/dated power schedule | — | configured from the web UI |

### 1a. Confirmed on the projector **[hw]**

With the projector on (its wired IP / MAC — redacted here): TCP 22
(the rooted image's dropbear), 80, 843, 4352, 4661, 8080 and 41794 are open.
PJLink answers in lower case: `pjlink 0`, `%1name=DELL S718QL`, `%1inf1=Dell`,
`%1inf2=` (empty), `%1clss=1`, `%1inst=31 32 33 41 42 43 44 45 46 51`,
`%1powr=1`, `%1inpt=31`, `%1avmt=30`, `%1lamp=41` (hours only, no on/off
field), `%1erst=000000`. The `INF1` reply is followed by stale buffer bytes
after its CR, so a client must discard anything after the first CR of each
reply. In standby the host stops answering entirely.

`atop_main` is a generic module shared across projector families; many strings
(D-sub, S-Video, lamp modes) cannot apply to this model. Which commands the
S718QL actually honours has to be found on hardware.

The NXP MCU's own `#00500`/`#00501` serial commands (WoL flag,
[nxp-mcu.md](nxp-mcu.md)) use the same `#00xx` style as the AMX set but are
handled by the MCU itself, on the DB9.

### The port-8080 "RS232" page is probably dead

`thttpd` (port 8080, docroot `/mnt/user1/thttpd/html`) serves the EZCast pages,
including `rs232_ctrl.html` → `cgi-bin/rs232cmd.cgi?cmd=<category>&value=<item>`.
The CGI passes the command over a SysV message queue to `rs232_main.app`, which
looks it up in `/mnt/user1/rs232/1.cfg` and writes `\r*<cmd>#\r` to
`/dev/uartcom` (BenQ-style: `pow=on`, `sour=hdmi`, `vol=+`, …; the parser is
even named `benq_result_parser`). **[code]** Nothing in the rootfs starts
`rs232_main.app`, and the BenQ ASCII protocol is not what the rest of this
projector speaks, so treat this as an SDK leftover. **[inferred]**

## 2. Standby and the network

The network going away in standby is governed by the NXP MCU's standby mode
(eco / normal / WoL) — see [nxp-mcu.md](nxp-mcu.md), "So what happens to the
network in standby?". New facts from this pass: **[code]**

- The NXP only ever *reads* the standby-mode byte (settings field 3); its only
  settings write is the WoL flag (`#00500`). Something else on the I²C bus
  writes the mode. Which processor does is not traced.
- The AM8250's own EEPROM driver (`at24_i2c.ko`, a 24C02 holding the MAC) is a
  different, smaller EEPROM than the NXP's settings EEPROM.
- `fui.app` has a `wake_on_lan_thread` that runs `/am7x/bin/wol.app`, and a
  `sys_Standby` path (`now is to enter standby`); `atop_main` has a Crestron
  status string `Standby State. Only network electronics remain on.` So the
  network board is designed to be able to stay up in standby.

Settled on hardware (2026-10-12) **[hw]**: the firmware has a configuration
option that keeps the network side powered in standby. With it on, the
projector answers nothing while suspended (PJLink times out) except a
**Wake-on-LAN magic packet, which powers it on** (owner's report). Without
it, the network simply disappears in standby. No MCU patch was needed.

## 3. Home Assistant

`ha/custom_components/dell_s718ql` is a Home Assistant custom integration:
PJLink polling for state (power, input, mute, light-source hours, errors),
PJLink commands while the projector answers, and a magic packet for power-on
when it does not. "No answer" is reported as standby/off rather than
unavailable. Install and behaviour: [`ha/README.md`](../ha/README.md). Tested
inside Home Assistant's test harness against a fake projector; **not yet run
against the real one**.

Home Assistant's built-in PJLink integration would also work while the
projector is on, but has no Wake-on-LAN.
