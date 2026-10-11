# S718QL system architecture

The S718QL is not one computer — it is **six independent processors**, each with
its own firmware, flashed by its own mechanism. Understanding which processor
owns a given behaviour is the whole game: it decides both how hard a change is
and how you'd deliver it.

This document is built up from static analysis of the M0R005 images plus Dell's
firmware upgrade guide. Anything marked *(inferred)* is a hypothesis from
strings/structure, not yet confirmed by disassembly.

## The six processors

```
                                  ┌──────────────────────────────────────┐
                                  │  HDMI 1 (2.0) / HDMI 2 (1.4) / HDMI3  │
                                  └───────────────────┬──────────────────┘
                                                      │ video
                    RS232 ─────┐                      ▼
   IR remote ─┐                │        ┌───────────────────────────────┐
   Keypad ────┼──►  ┌──────────┴─────┐  │   MStar MST9U3  "Scaler"       │
              │     │ NXP LPC11U67   │  │   (Projector FW / MERGE.bin)   │
              └────►│ "System MCU"   │◄─┤   • OSD / menu / UI            │
                    │ Cortex-M0      │  │   • input switching, scaling   │
                    │ • power seq    │  │   • picture (PQ), keystone     │
                    │ • fans/thermal │  │   • HDMI-CEC pad               │
                    │ • IR decode    │  │   • USB firmware upgrade       │
                    │ • standby rail │  └───────────────┬───────────────┘
                    └───┬────────┬───┘                  │ processed video
                        │ I²C    │ UART                 ▼
                        │        │          ┌───────────────────────────┐
                        ▼        ▼          │  TI DLPC4422 "DDP"/"DMD"   │
              ┌──────────────────────┐      │  (DDP FW / *.img)          │
              │ Nuvoton M451         │      │  ARM, on the FORMATTER bd  │
              │ "Formatter MCU"      │◄────►│  • DMD (micromirror) drive │
              │ Cortex-M4            │ I²C  │  • laser/lamp ballast      │
              │ • I²C path switch    │      │  • colour/sequence, PWM    │
              │ • EEPROM, GPIO       │      │  • local keypad/temp I²C   │
              │ • talks to DLPC/M054 │      └────────────┬──────────────┘
              └──────────────────────┘                   │ drives
                                                         ▼
                                              ┌────────────────────┐
                                              │  DMD + laser engine │
                                              │  (the light path)   │
                                              └────────────────────┘

   ── "smart"/network side, largely independent of the projection path ──

   ┌────────────────────────────┐        ┌────────────────────────────────┐
   │ Actions Micro AM8250 (MIPS)│        │ Realtek RTD1295 (ARM64)         │
   │ "Network FW" ACTUPGRADE.bin│        │ "Actiontec FW" install.img      │
   │ • EZCast Pro stack         │        │ • ScreenBeam wireless display   │
   │ • the "Network" source     │        │ • Android-based (SBDP1.05)      │
   │ • web config UI (thttpd)   │        │ • Miracast / casting receiver   │
   │ • Linux 2.6.27, busybox    │        │ • Wi-Fi (RTL8822BE)             │
   └────────────────────────────┘        └────────────────────────────────┘
```

### 1. MStar MST9U3 — "Scaler" (Projector FW)
The video brain and the thing the user actually interacts with. It takes the
HDMI inputs, scales/processes them, renders the **OSD menu** and all on-screen
messages, drives keystone/warp, and hands processed video to the formatter. It
also hosts the **USB firmware-upgrade** path (the "copy MERGE.bin to a USB
stick" flow). Internally it's three pieces of code in one image: an 8051-based
**PM (power management) core** for standby, a boot loader (`sboot`), and the
large main application ("AP") that runs the UI. Build stamp in this image:
`Jun 29 2017` / later strings `Feb 02 2017`. CPU is MStar's proprietary core
(*inferred*: the "aeon" family, an OpenRISC-derived variable-length ISA — not
stock ARM/MIPS), which is the main obstacle to code-level patching.

### 2. TI DLPC4422 — "DDP" on the Formatter board (DDP FW)
**What a "formatter board" is:** in a DLP projector the imager is a **DMD**
(Digital Micromirror Device) — a chip with millions of tiny mirrors, one per
pixel, that tilt toward or away from the light thousands of times a second. The
*formatter board* is the board that carries the DMD controller(s) and drives
those mirrors: it takes the incoming processed video and converts it into the
precisely-timed mirror on/off sequences (plus bit-plane/colour sequencing) that
produce a grey-scale/colour image, and it manages the **light source** (here a
laser, via a ballast driver) and the colour pipeline. On this projector that
controller is the **TI DLPC4422** (a dual-channel DLP controller for 0.66" 4K
UHD DMDs). Its firmware image contains the DMD "sequences", colour/gamma tables,
laser ballast control (OSRAM/dynamic black), temperature (TMP75) and keypad
(TCA8418) I²C handling, and a small on-DMD **8051** ("DDCRAM"). It's flashed
with TI's Windows GUI (DLPC4422-GUI) over mini-USB.

### 3. NXP LPC11U67 — "System MCU" (NXP MCU FW)
The always-on **housekeeping controller**. *(Inferred from role + the Nuvoton
side's messages)*: it owns the **power sequence**, fan/thermal supervision, the
**IR remote** decode and front **keypad**, and the standby power rail. It is the
chip that decides the projector turns on when you press Power. The image is a
`.hex` for a Cortex-M0, with a separate **BootCode** region at 0x0 and the
application at 0x2000 (so it has its own bootloader + app split). Flashed over
**RS232 at 19200 baud** with `PIC_NXP_Downloader.exe`. Dell's docs call it the
"NXP MCU"; the download tool UI still says "PIC/NXP" and reports the part as
`LPC11U67`.

### 4. Nuvoton M451 — "Formatter MCU" (Formatter MCU FW)
A second MCU (Cortex-M4, part `M451RD3AE`) that sits on/near the formatter board
and acts as the **I²C traffic cop and glue**. Its strings show it switching I²C
"paths" (`GPIO_Set_Path_Switch`), driving an EEPROM, relaying UART commands,
and coordinating the DLPC download mode. Crucially it reacts to events from the
NXP MCU (referred to as "M054"): `@PowerOn event from M054!!`,
`DDP DownLoad Mode event from M054!!`, `Power Off`. It also carries a
**casting/ID string** `AMXB<-SDKClass=VideoProjector><-Make=Smart><-Model=NEBULA-P1>`
— another tell that this is XGIMI/Nebula-derived hardware. Flashed over **RS232**
with Nuvoton's `NuvoISP.exe`. Source path: `XGIMI_T1_M451_AppCode`.

### 5. Actions Micro AM8250 — "Network FW"
A MIPS SoC running **Linux 2.6.27** (busybox userland) with the **EZCast Pro**
application stack. This is the **"Network" input source** in the menu: the web
configuration UI (`thttpd`), network projection, and its own OTA path that reads
`ACTUPGRADE.BIN` off USB. Version strings: `AM8250`, product `EZ.BOX`,
`ezcastpro_projector`, build `20180917`. This is one of the two realistic
targets for running custom code (see feature targets c/d).

### 6. Realtek RTD1295 — "Actiontec FW"
A quad-core ARM64 (Cortex-A53) Realtek media SoC running an **Android-based**
system — the **ScreenBeam** wireless-display receiver (Actiontec is the
ScreenBeam vendor). Package version **SBDP1.05**, Wi-Fi via **RTL8822BE**. This
is the more powerful of the two Linux-class processors and the better candidate
for "run our own streaming code" if we can get in.

## Interconnect (how they talk)

- **Video path:** HDMI → MST9U scaler → DLPC4422 formatter → DMD/laser.
- **Control plane:** the NXP system MCU is the hub for power/IR/keypad. The
  Nuvoton formatter MCU exchanges events with it (`...event from M054`) and
  bridges **I²C** to the DLPC4422 and EEPROM, switching I²C paths as needed.
- **Scaler ↔ MCUs:** the scaler AP has UART command handlers and a
  `MCU_HDMI2_14_Switch` call and reads a "`Netwrk Version`", i.e. it coordinates
  with both the MCUs and the network module.
- **RS232 (external, DB9):** the service/control serial port. Both MCUs are
  flashed through it, and it's the documented external control channel. This is
  the same port the user is driving from a microcontroller today (feature d).
- **USB Type-A:** user-facing; used for the scaler/network/actiontec USB flash
  upgrade flows and for USB media.
- **Ethernet / Wi-Fi:** owned by the network + actiontec SoCs. The EMEA models'
  "wake on LAN application required to power on from network standby" note in
  the Dell listing is the key hook for feature d.

## Boot & power sequence *(inferred — to be confirmed)*

1. **AC applied.** The NXP system MCU + the scaler's 8051 PM core come up on
   standby power. The projector sits in **standby** (Power LED). In standby the
   main SoCs and (per the user's observation) the Ethernet PHY are powered down
   — which is exactly why remote wake currently fails.
2. **Power pressed (or wake event).** NXP MCU runs the power-on sequence, brings
   up rails, and signals the Nuvoton formatter MCU
   (`@PowerOn event from M054!!`). Fans spin (fan states: Initial → Normal).
3. **Scaler boots:** `sboot` loader → decompresses the AP into DRAM → AP starts,
   initialises the panel/timing, OSD, and inputs.
4. **Formatter brought up:** Nuvoton MCU + DLPC4422 initialise the DMD and laser
   ballast; colour sequences load. Scaler begins sending video.
5. **Smart SoCs:** the AM8250 (network) and RTD1295 (actiontec) boot their own
   Linux/Android independently when needed (e.g. selecting the Network source).
6. **Power off:** reverse — scaler shows the shutdown message, DLPC runs the
   cooling fan states (`Cooling-Normal`/`Cooling-AB`) before the laser/power is
   cut.

## Open questions

- Exact ISA of the MST9U AP core and whether a disassembler exists for it.
- The precise NXP↔Nuvoton↔scaler wire protocol (which bus carries
  power/standby commands, and the framing) — central to features d and f.
- Whether the RS232 control protocol flakiness (feature d/f) is in the NXP MCU's
  UART handler or in the scaler's.
- Whether the Ethernet PHY can be kept powered in standby (feature d).
