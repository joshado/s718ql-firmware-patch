# NXP LPC11U67 "System MCU" — disassembly notes

Reverse-engineering notes for `Dell-s718ql-mcu-firmware-e01-2018-11-30.hex`
(flat image `firmware/unpacked/mcu/nxp_lpc11u67.bin`), written while answering
"can the network stack be kept up in standby?" (feature **d**).

Everything here is from **static analysis only** — nothing has been flashed or
measured. Labels:

- **[C]** confirmed by reading the code (the code does this);
- **[I]** inferred (what a pin/UART is physically wired to, what a setting is
  called in the OSD). Every **[I]** needs a meter, a scope or an experiment.

All addresses are flash addresses, which equal file offsets in the flat `.bin`.

A function-by-function C rendering of the whole application is in
[`decompiled/nxp_app.c`](../decompiled/nxp_app.c), with the symbol table in
`decompiled/nxp_app_symbols.tsv`. It is a reading aid: not buildable, not
verified against the binary.

## Reproducing the disassembly

```sh
python3 -m venv /tmp/re && /tmp/re/bin/pip install capstone
B=firmware/unpacked/mcu/nxp_lpc11u67.bin
/tmp/re/bin/python -I tools/nxp_disasm.py   $B 0x2000 firmware/unpacked/mcu/nxp_app.txt
/tmp/re/bin/python -I tools/nxp_disasm.py   $B 0      firmware/unpacked/mcu/nxp_boot.txt
/tmp/re/bin/python -I tools/nxp_trace.py    firmware/unpacked/mcu/nxp_app.txt > firmware/unpacked/mcu/nxp_trace.txt
/tmp/re/bin/python -I tools/nxp_initdata.py $B firmware/unpacked/mcu/nxp_data_init.bin
```

`nxp_app.txt` is the full listing (12,218 instructions, 307 functions);
`nxp_trace.txt` is the readable one — per function, every call with its
constant arguments (`gpio_wr(P1_5, 0)`, `delay_ticks(1000)`, RAM flag accesses).
Outputs are derived from the vendor blob, so they stay under `firmware/`
(git-ignored).

## Image layout [C]

| Range | Contents |
|---|---|
| `0x0000`–`0x1f00` | BootCode. Own vector table, SP `0x100007e8`, reset `0x123c`. Uses USART0, USART1/4, USART2/3, CT16B0 and I2C1 IRQs, IAP (`0x1fff1ff1`). |
| `0x2000`–`0x9a00` | Application. Vector table at `0x2000`, SP `0x10001dc0`, reset `0x2148` → Keil `__main` `0x20c0` → `main` `0x88d8`. |
| `0x997c` | Scatter-load region table: `.data` src `0x999c` → `0x10000100` len `0x1fc` (zero-run RLE, decompressor `0x23d6`); `.bss` `0x100002fc` len `0x1ac4`. |
| `0x1ff00` | Config word `aa 20 c1 00 c1 00` — read **and written** by the BootCode (`0xe5a`, `0x10f0`). Looks like an "app valid" marker plus version `c1.00`. |

Toolchain is Keil/ARMCC (switch8 helper at `0x87ec`, `__aeabi_uidivmod` at
`0x222a`). See "BootCode, recovery and debug access" for what the BootCode
checks before running the app.

## Peripherals in use [C]

| Peripheral | Init | IRQ handler | Notes |
|---|---|---|---|
| USART0 | `0x588c` | `0x829c` | P0_18/P0_19. `0xAA`-framed link with a table of up to six devices (6-byte address + name); parser task `0x2a94` / `0x2814`. Only brought up with the P2_23/P1_15/P1_18 rails |
| USART1 | `0x595c` | `0x833c` (shared 1/4) | P0_13/P0_14. `BE EF` frames (Dell's RS232 control format), task `0x60a0` → `0x2b28` → `0x3c20` |
| USART2 | `0x5a24` | `0x8448` (shared 2/3) | P1_0/P1_6. ASCII `#…\r` commands **and** `BE EF` frames, task `0x6b48` |
| USART3 | `0x5af4` | `0x8448` | P2_3/P2_4 |
| USART4 | `0x5bbc` | `0x833c` | P2_11/P2_12. `BE EF 10` frames, task `0x6150` → `0x3fc8`; brought up late by `0x6fc0` |
| I2C1 | `0x56a4`… | `0x4d98` | P1_11/P1_14, own address `0xb0`, 100 kHz. **Slave** for the 93-case command switch at `0x2bfc` (task `0x2b70`; frame `id, n, payload, xor`, id `0xF1` = command) **and master** for the external EEPROM. Probably the Nuvoton's `M054_Read/Write` link [I] |
| I2C0 | | `0x4968` | P0_4/P0_5, own address `0x66`. Second **slave** command channel (task `0x4c80`, 2-byte frames: 3 = fan/selector, 4 = `led_mode`, 5 = off sequence, 6 = `power_on`) |
| CT16B1 / CT32B0 / CT32B1 | `0x8064` | `0x7c34` / `0x7d0c` / `0x7e48` | timers; CT32B0 logs edge intervals on P2_9. IR is **not** here: NEC decode on P1_1 via PINT3 (`0x6816` → `0x524c`) |
| SCT0 | `0x6d3c` | `0x6cf4` | PWM (fans / LED) [I] |
| PINT0-7, GINT0/1 | | `0x6780`… / `0x4574`,`0x4598` | pin interrupts |
| SysTick | `0x7f38` | `0x7bd4` | **0.1 ms tick** (reload 4800 at 48 MHz); `delay_ticks` = `0x8130` (busy-wait on `0x100001a4`) |

All UARTs run at **19200** (`0x4b00`). There is no PMU / deep-sleep use: standby
is the main loop spinning with `wfi` at `0x8f14`, MCU fully awake.

## GPIO

Helpers [C]: `gpio_rd(id)` `0x4780`, `gpio_wr(id, val)` `0x48ba`,
`gpio_dir(id, out)` `0x485c`, `gpio_as_input(id, …)` `0x46b4`,
`iocon_cfg(port, pin, pull)` `0x40e8` (1 = pull-down, 2 = pull-up). **Pin id = `(port << 8) | pin`**, so
`0x109` = P1_9, `0x20f` = P2_15. Pin setup table at `0x96b4`, 8 bytes per pin:
`u16 id, u8 iocon_func, u8 dir, u8 mode, 3 pad` (80 entries, P0_0–P2_23); `dir` 1 = output (initial level = mode bit 0), 0 = input (pull = mode bits 3:2), other values = left alone.

Pins that matter for power and standby. The "role" column is **[I]** unless
marked — it is deduced from the order things are switched in:

| Pin | Behaviour in code [C] | Likely role [I] |
|---|---|---|
| P1_25 | first thing raised in power-on (`0x856c`); dropped in power-off | main supply enable |
| P0_7 | 1 during cooling and "normal" standby; 0 in eco standby; 0 for 100 ms before the standby rails are raised | standby-rail discharge / reset for the group below |
| P1_9, P1_5, P1_7 | the "standby rail group": raised together on power-on, **left high in normal standby, dropped in eco standby** | supplies for the always-on smart/network side |
| P1_5 | additionally pulsed low for 100 ms to leave WoL mode (`0x65f4`) | Ethernet PHY reset or PHY supply |
| P1_29 / P1_30 | bit-banged clock / data, clause-22 frames (`0x916c` read, `0x9388` write) | **MDC / MDIO to the Ethernet PHY** [C that it is MDIO] |
| P0_3 | 0 while on; 1 after the MCU has armed WoL | MDIO mux / PHY isolate (MCU owns the PHY) |
| P0_2 | 1 in WoL standby, 0 on power-on | WoL-mode supply / mux select |
| P1_28 | input, polled in standby by `0x6734` | **PHY PMEB (magic packet seen)** |
| P1_16 | input, debounced by `0x5d40` → flag `0x1000018e` | power key |
| P0_20 | input; selects between `P1_8=0,P0_22=1` and the alternative | |
| P1_27 | input, checked while on (`0x7094`) → forced shutdown with LED mode 9 | fault / thermal / lid |
| P2_15, P1_16 | read in the off path of the main loop; with P2_15 high and the power key held, an IR code selects a factory test | factory-test entry |
| P1_24, P1_19, P1_10, P1_8, P0_22, P1_31, P0_6, P0_8, P1_15, P1_18, P2_23, P2_17, P0_21 | sequenced in power-on `0x8564`, dropped in power-off | main-side rails / resets / laser enables (P2_23, P1_15, P1_18 are a 30 ms / 50 ms / 1 s sequenced trio) |
| P1_12/P1_13, P1_22/P1_23, P2_20/P2_21 | 2-bit selectors (`0x7850`, `0x776e`, `0x77e0`) | fan speed / mux selects |
| P2_2, P2_6, P2_8, P2_9, P2_10, P2_18 | driven by `led_mode` `0x3cf0` | LEDs |

## External EEPROM and the settings block [C]

- On **I2C1**. Byte read `0x64f4`, byte write `0x655c`, multi-byte read `0x64c8`, write
  `0x6530`. Address → device/page split in `0x8808`, using a geometry table at
  `0x9934` (index `0x10000277` = 5) and the I2C device address in
  `0x10000276`, **default `0xAC`**.
- `load_settings` `0x445c` reads a 10-byte pointer table at EEPROM `0x3ff6`
  into `0x10001a00` (five `u16` addresses), then reads each field:

| # | RAM | Len | Getter | Meaning |
|---|---|---|---|---|
| 0 | `0x10001a44` | 1 | `0x7214` (true if == 1) | **WoL enable** [C: it gates the PHY WoL code] |
| 1 | `0x10001a45` | 6 | `0x43b8` | **MAC address** programmed into the PHY [C] |
| 2 | `0x10001a4b` | 1 | | unknown |
| 3 | `0x10001a4c` | 1 | `0x71a4` (true if == 0) | **standby mode: 0 = keep standby rails, non-zero = eco** [C] |
| 4 | `0x10001a4d` | 1 | `0x71d0` (true if == 0) | used by `led_mode` |

- `0x10000278` (`eeprom_ac_ok`, default 1): set from whether the **first** read
  at address `0xAC` succeeded. If it failed the code falls back to device
  `0xA0` and `eeprom_ac_ok` stays 0, which disables every WoL path. So "is
  there an EEPROM at 0xAC" is the hardware/region switch — a good candidate for
  the "EMEA models only" wording in Dell's listing [I].
- **Likely OSD names** [I]: the scaler's Power Settings menu has `Power Mode`
  (`Normal` / `Eco`), `Network` (`Standby On` / `Standby Off`), `Auto Shutdown`
  and `Power On Beep`. `Power Mode` is the obvious match for setting 3
  (0 = Normal = keep rails, non-zero = Eco). `Network` standby is probably one
  of the other bytes (setting 2 or the WoL enable). Not traced through the
  scaler code.
- Settings are re-read on every power-off and again in standby
  (`0x7050`, 0.5 s in), so another processor can change them while the projector is on.
- Other EEPROM cells: `0x3e00` (bit flags, `0x7730`), `0x3e06` (factory mode).

## ASCII command set on USART2 [C]

Task `0x6b48`: a line starting `#`, ending `\r`, compared (8 bytes) against the
three strings at `0x9958`:

| Command | Action | Reply |
|---|---|---|
| `#00500 0\r` | WoL enable := 0, written to EEPROM (`0x4508`) | `P\r` |
| `#00500 1\r` | WoL enable := 1, written to EEPROM | `P\r` |
| `#00501 1\r` | query WoL enable | `P0\r` or `P1\r` |
| anything else | | `F\r` |

USART2 is very probably the **DB9** (the BootCode's serial download, which
Dell's guide performs over the DB9, uses USART2 — see "Serial download
protocol"), and it is the one UART (re)initialised on *every* entry to standby.
So these commands are meant to be sent from outside; the literal `#00500` does
not appear in the AM8250 rootfs, the scaler image or the Nuvoton image.

## Power state machine [C]

Flags: `0x100002e8` powered-on, `0x100002e9` state/fault code, `0x100002ea`
wake reason, `0x100002f8` "standby rails were left up", `0x100002e1` PHY WoL
state (2 = armed), `0x100002f6` "standby settled".

`main` (`0x88d8`) calls `power_off(1)` once at boot, then loops. While off:

```
if !standby_settled():            # 0x7050: 0.5 s after entry, re-reads settings,
    continue                      #         eco → P0_7 = 0
key  = power_key_event()          # 0x5d18, wake reason 2
ir   = ir_code()                  # 0x51d0, code 2 = power, wake reason 3
uart = take(0x1000012c) == 0      # 0x6adc, wake reason 4
if eeprom_ac_ok && wol_enabled() && phy_pme_fell():   # 0x6734, wake reason 7
    wake
if wake: power_on()               # 0x7278
wfi
```

Wake reason 4 is raised by `0x6a68(1)` and `0x6590(0x4e)`, both reached from
the `BE EF` frame handler `0x3c20`, which is fed by **USART1** (`0x60a0`) and by
non-`#` traffic on **USART2**. So a power-on command over a UART already works
in standby, as long as that UART is still enabled.

### Standby entry — tail of `power_off(mode)` `0x7370`

After a wait (`P0_7 = 1`, 2 s), tearing down the main side and reloading
settings:

```
0x7444  if keep_rails():                       # setting 3 == 0
            if !(eeprom_ac_ok && wol_enabled()):
                uart1_init(19200)              # USART1 stays alive
        else:                                  # eco
            P0_13, P0_14 -> GPIO inputs        # USART1 pins released
0x74a4  uart2_init(19200)                      # always
        ...
0x7572  if !keep_rails():                      # ---- eco standby
            rails_up = 0
            P0_7 = P1_5 = P1_7 = P1_9 = 0
            if eeprom_ac_ok && wol_enabled():
                P0_3 = 0; phy_state = 0; P1_29 = P1_30 = 0
        else:                                  # ---- normal standby
            rails_up = 1
            if eeprom_ac_ok && wol_enabled():  # ---- WoL standby
                P0_3 -> input, 0; phy_state = 0
                P0_7 = P1_5 = P1_7 = 1; delay 100 ms
                P1_29 = P1_30 = 1; P0_7 = 0; P0_2 = 1; P1_29 = P1_30 = 0
                phy_arm_wol()                  # 0x6620
                P0_3 -> output, 1
            elif mode == 1:                    # first entry after AC-on
                P0_7 = P1_9 = P1_5 = P1_7 = 1
            # otherwise: rails simply left as they were while on
```

`phy_arm_wol` (`0x6620`), PHY address 3, is the Realtek RTL8201F-family
sequence: wait for reg 0 bit 15 and reg 1 bit 4 to clear; page `0x12` regs
`0x10`–`0x12` := MAC; page `0x11` reg `0x11` := `0x1fff`, reg `0x10` :=
`0x1000` (magic packet); page 7 reg `0x14` |= `0x8000`; page `0x11` reg `0x13`
|= `0x8000`; page 7 reg `0x13` |= `0x0400` (pin → PMEB).

### Power-on — `0x7278` → rail sequencer `0x8564`

`uart0/1/3_init(19200)`, then `P1_25 = 1`, then:

- WoL was armed: `P0_3 = 0`, pulse `P1_5` low 100 ms, `P1_9 = 1`, `P0_2 = 0`.
- Rails were dropped (`rails_up == 0`): `P0_7 = 0`, 100 ms, `P1_9 = P1_5 = P1_7 = 1`.
- Rails were kept: nothing to do.

Then 0.5 s + 0.7 s of delays and the main-side sequence (`P1_24`, `P1_19`, …).

## So what happens to the network in standby?

The MCU already implements three standby flavours, chosen by EEPROM settings:

| Mode | Condition | Standby rail group | USART1 | PHY |
|---|---|---|---|---|
| Eco | setting 3 ≠ 0 | **off** | pins released | off |
| Normal | setting 3 = 0, WoL off or no `0xAC` EEPROM | **left on** | alive, can wake the MCU | untouched |
| WoL | setting 3 = 0, WoL on, `0xAC` EEPROM present | on | not re-enabled | taken over by the MCU, magic-packet wake |

So the NXP does **not** unconditionally cut the network side. If Ethernet goes
dead in standby, one of these is true, in rough order of likelihood:

1. the unit is in **eco** standby (setting 3 ≠ 0);
2. the unit is in **WoL** standby, where the MCU isolates the PHY on purpose and
   only a magic packet to the stored MAC gets through;
3. the rails are up but the **AM8250 suspends itself** (`wol.app` logs
   `now is to enter standby` / `sys_Standby`) — then the fix is on the AM8250
   side, not here;
4. the AM8250 is fed from a main-side rail that `power_off` always drops
   (`P1_25`, `P1_24`, `P1_19`, …) rather than from the standby group.

### Experiments, cheapest first (no flashing)

1. On the DB9 at 19200 8N1, in standby, send `#00501 1\r`. The DB9 is very
   probably USART2, so expect `P0\r` / `P1\r`: the WoL flag.
2. In the OSD, Power Settings: set `Power Mode` to `Normal` and `Network` to
   `Standby On`; see whether the link LED and ping survive standby.
3. With WoL on, send a magic packet to the projector's wired MAC.
4. Meter the rails gated by P1_5 / P1_7 / P1_9 / P1_25 in standby vs on, and
   find which one feeds the AM8250 and the PHY. This settles case 4.

### Patch points, if experiments say the MCU is the blocker

All are 4-byte edits in the app region; none touches the BootCode.

| Goal | Address | Original | Patch | Effect |
|---|---|---|---|---|
| Never use eco standby | `0x71a4` (`keep_rails`) | `08 b5 68 46` | `01 20 70 47` (`movs r0,#1; bx lr`) | standby rail group stays up and USART1 stays alive whatever setting 3 says. Three callers: `0x7068`, `0x7444`, `0x7572`. |
| Never let the MCU take the PHY | `0x7214` (`wol_enabled`) | `08 b5 68 46` | `00 20 70 47` (`movs r0,#0; bx lr`) | disables the WoL takeover so the AM8250 keeps its PHY; also makes `#00501 1` report 0 and removes wake reason 7. |
| Keep a main-side rail in standby | the `gpio_wr(pin, 0)` call in `0x7370` / `0x6960` for that pin | `bl 0x48ba` | `nop; nop` (`c0 46 c0 46`) | only once step 4 above has identified the pin; mind the matching sequencing in `0x8564`. |

With the first two patches the intended behaviour is: AM8250 and PHY stay
powered, the AM8250 keeps its network stack up, and it powers the projector on
by sending the existing power-on frame to the MCU over USART1 (wake reason 4).
Whether the AM8250 itself stays awake is a separate question (case 3).

Costs to keep in mind: higher standby draw, and whatever is on those rails
running 24/7.

## BootCode, recovery and debug access

From reading the BootCode (`0x0000`–`0x1f00`); nothing here has been tried on
hardware.

**Boot decision** (`boot_main` `0x46c`) [C]:

1. Config byte at `0x1ff00` (`0xe58`): if it is not `0xAA` the BootCode erases
   sectors 2–24 (`0x2000`–`0x1ffff`) and waits for an update. `0xAA` is written
   last (`0x10e4`–`0x1102`), so an interrupted update leaves the unit in update
   mode rather than running half an app.
2. Key combo at reset (`0x376`), reading P2_15 and P1_16 (the power key):
   both held → update mode at once; one held → LED feedback, and after about 40
   polls returns mode `0x2x`, after 60 mode `0x3x`. Dell's guide matches: hold
   Power, plug in AC, release when the LED blinks white after 4–6 s.
3. A mailbox byte at the top of RAM (`0x10003fff`, accessors `0xe04`/`0xe14`)
   can also request update mode (`0xff`).
4. Otherwise jump to the app (`0xe84`): requires only that the reset vector at
   `0x2004` lies within `0x2000`–`0x12000`; copies 52 vectors to RAM
   `0x10000000`, sets `SYSMEMREMAP = 1`, loads MSP, jumps. **No checksum over
   the app was found on this path.** If the vector is out of range it blinks
   P2_9 forever.

**What protects the BootCode** [C]: every IAP prepare/erase call in it uses
sectors 2–24 (`0x164`/`0x1ae` with `(2, 0x18)`), so the serial update path
cannot erase sectors 0–1. The update UART is 19200 baud (`0x157e`).

**Below the BootCode** [C from the image, I for the board]:

- CRP word at `0x2fc` is `0xFFFFFFFF`: no code read protection. SWD and the
  NXP ROM ISP are both enabled as far as the chip is concerned.
- The pin table leaves P0_10 and P0_15 on function 0 (SWCLK / SWDIO), so the
  app does not repurpose the debug pins. Whether they reach pads or a header
  on the board is unknown.
- ROM ISP: P0_1 low at reset enters NXP's mask-ROM loader (UART on
  P0_18/P0_19, or USB mass storage if P0_3 is high). This works even with the
  BootCode destroyed. Board access to P0_1 is unknown.
- The `.hex` has `0` in the vector checksum slot at `0x1c` and the first eight
  vectors do not sum to zero, which the ROM would treat as "no valid user
  code". So the BootCode bytes in the `.hex` are not what is programmed on a
  working unit as-is; the downloader most likely skips that region. Do not
  program the `.hex` verbatim with an SWD tool without fixing `0x1c`.

**Debug output** [C]: the app contains no log strings or printf path. The only
text is the three `#0050x` commands and the BootCode's `Update` / `Reset` /
`BootCode` lines. There is no serial console to tap; visibility comes from SWD
or from code we add.

## Serial download protocol

Read from both ends: Dell's `PIC_NXP_Downloader.exe` (a .NET program, namespace
`MCU_Downloader`, method `LPC11U67_Update`) and the BootCode's receive loop
(`0x6c8`–`0x7de`). The two agree. **Not exercised on hardware.**
`tools/nxp_flash.py` implements it and has a simulator of the device side.

**Link:** 19200 8N1 on **USART2** (`0x40070000`, P1_0/P1_6), polled, no flow
control, no timeouts on the device side. Dell's procedure uses the DB9, so
**the DB9 is USART2** — the same UART that takes the `#0050x` and `BE EF`
commands in standby [I, from the procedure; C that the BootCode uses USART2].

**Entering it:** hold Power while applying AC and release when the LED blinks
(BootCode mode `0x21`, key held for 40–59 polls). The BootCode then raises
some rails (P1_25, P1_19, P1_31, P0_8), waits about 1.5 s, and only then
starts reading the UART. Holding for 60+ polls gives mode `0x31`, which takes
a different path (`0x622`) that has not been read.

| Step | Host sends | Device replies | Device action |
|---|---|---|---|
| Sync | `AA 55` | `14` | device id: `0x14` = LPC11U67 (`0x13` LPC1113, `0x02`/`0x06` PIC24). **Any other first two bytes → system reset** |
| Write | `03` + 1024 data bytes | `01` ok, `00` fail | on the first block (pointer = `0x2000`) erase sectors 2–24; then IAP copy-RAM-to-flash at the pointer; pointer += 1024 **even on failure** |
| Run | `00` | — | jump to the app (`0xf06`) |
| Restart | `CC` then `AA 55` | `14` | pointer back to `0x2000` |
| other byte | | `00` | ignored |

What the host does (and `nxp_flash.py` copies): parse the Intel HEX into a flat
image filled with `0xFF`, length rounded up to 1024; send every block from
`0x2000` to the end (capped at `0x20000`) — for the Dell file that is 120
blocks, including all-`0xFF` ones; wait up to 5 s for each ack; send `00`.

Things that follow from this [C]:

- **No checksum, CRC or read-back anywhere.** The only check is the IAP
  call's status. A corrupted byte on the wire is flashed silently.
- **There is no address in the protocol.** Blocks land sequentially from
  `0x2000`, so the BootCode region cannot be addressed at all, and the
  bytes below `0x2000` in the `.hex` are never sent.
- The "app valid" byte at `0x1ff00` is simply part of the last block. An
  aborted transfer leaves it erased, and the next boot erases the app area
  and waits for a download.
- On the `00` command the BootCode jumps to the app; if the reset vector at
  `0x2004` is out of range it blinks P2_9 forever instead (power-cycle and
  re-enter download mode).
- The device does not echo or time out, so a host that sends `AA 55` before
  the BootCode is listening loses a byte (USART2 has no FIFO) and the unit
  resets on the next mismatch. Wait for the blinking LED first.

There is a second loader in the BootCode (`0x7e0` onward, parser `0xfca`)
that takes ASCII lines beginning `U`, `R` or `:` — Intel HEX over serial, with
the `Update` / `Reset` / `BootCode` strings. It is reached in the other boot
modes and has not been decoded.

## Function index

| Address | Name | |
|---|---|---|
| `0x88d8` | `main` | factory-test branches up front, main loop from `0x890e` |
| `0x7278` | `power_on` | |
| `0x8564` | `power_on_rails` | |
| `0x7370` | `power_off(mode)` | mode 1 = boot, 2 = fault shutdown |
| `0x7050` | `standby_settled` | |
| `0x7094` | `on_state_monitor` | fault pin P1_27 |
| `0x6fc0` | late bring-up while on | USART4 init, P0_20 |
| `0x4c80` | I2C0 slave command task | |
| `0x445c` | `load_settings` | |
| `0x71a4` / `0x7214` / `0x71d0` | `keep_rails` / `wol_enabled` / setting 4 | |
| `0x492c` | `eeprom_ac_ok` | |
| `0x6620` / `0x65f4` / `0x6734` | `phy_arm_wol` / `phy_leave_wol` / `phy_pme_fell` | |
| `0x916c` / `0x9388` | `mdio_read(phy, reg)` / `mdio_write(phy, reg, val)` | |
| `0x2b70` | I2C1 slave command task | 93-case switch at `0x2bfc`, command byte = `buf[2]` |
| `0x3c20` | binary key/command handler | |
| `0x6b48` | USART2 task | ASCII `#` commands |
| `0x6a04` | ASCII reply (`P`/`F`) | |
| `0x3cf0` | `led_mode(n)` | 23-case switch |
| `0x51d0` | `ir_code()` | |
| `0x5d40` | power-key debounce | |
| `0x7850`, `0x776e`, `0x77e0` | 2-bit GPIO selectors | |
| `0x394c`…`0x3bf8` | single-rail on/off setters | reached from the I2C1 command switch |

## Open questions

- The BootCode's second, ASCII/Intel-HEX loader (`0x7e0`, `0xfca`) and boot
  mode `0x31`.
- Are SWD (P0_10/P0_15) and the ISP pin P0_1 reachable on the board?
- Physical mapping of USART0–4 (DB9, scaler, AM8250, RTD1295, Nuvoton). In
  `power_state_code == 3` the tasks bridge UARTs transparently (USART0 → 2,
  USART4 → 2, USART2 → 1), which should help identify them.
- What the USART0 `0xAA` device link is. The scaler has a `Bluetooth Settings`
  menu (search / connect / connected device), so a Bluetooth module is likely.
- Which rail feeds the AM8250 and the PHY.
- Which processor writes settings 2–4 into the `0xAC` EEPROM, and the OSD names.
- Binary frame format on USART0/1/2 (`0xAA` start byte, `0x2814`), and the full
  I2C1 command table at `0x2bfc`.
- Sender of the `#00500` commands.
