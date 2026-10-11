/*
 * Dell S718QL "System MCU" (NXP LPC11U67, Cortex-M0) -- application region
 * 0x2000-0x9a00 of Dell-s718ql-mcu-firmware-e01-2018-11-30.hex,
 * hand-decompiled to C for reading.
 *
 * THIS FILE IS NOT BUILDABLE AND HAS NOT BEEN VERIFIED AGAINST THE BINARY.
 * It is a reading aid produced from tools/nxp_disasm.py output. Control flow,
 * constants and call order are meant to follow the disassembly, but nothing
 * has been compiled, diffed or run. Treat the disassembly as the authority
 * and check any function here against it before relying on it.
 *
 * Conventions
 *   - Each function is preceded by its flash address. Functions and RAM
 *     variables that could not be named keep fn_XXXX / g_XXXX (address).
 *   - Names are our guesses from behaviour, not vendor symbols.
 *   - `/* ? * /` marks a guess. Vendor bugs are kept as coded.
 *   - P(port, pin) is a GPIO pin id, (port << 8) | pin.
 *   - delay_ticks(n) busy-waits n SysTick periods. SysTick is 4800 core
 *     clocks at 48 MHz, so ONE TICK IS 0.1 ms: delay_ticks(20000) is 2 s.
 *     Some comments in the body still say "ms" or "s" on the older, wrong
 *     assumption of a 1 ms tick; trust the tick counts.
 *   - `settings` is the block loaded from the external EEPROM by
 *     load_settings(): wol_enable, mac[6], s2, standby_mode, s4.
 *   - The file is seven address ranges decompiled separately and joined, so
 *     local helper macros and commentary style vary between parts.
 *
 * See docs/nxp-mcu.md for the surrounding analysis.
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed int     s32;
#define P(port, pin)   (((port) << 8) | (pin))
#define REG32(a)       (*(volatile u32 *)(a))

/* ---- Function index (flash address, name, summary) ----
 * 0x20c0  rt_main                        Keil __main: scatter-load then __rt_entry
 * 0x20c8  rt_scatterload                 Keil __scatterload: walks region table 0x997c..0x999c (.data decompress, .bss zero)
 * 0x2120  rt_lib_init                    Keil __rt_lib_init stub (empty)
 * 0x2124  rt_lib_shutdown                Keil __rt_lib_shutdown stub (empty)
 * 0x2128  rt_entry                       Keil __rt_entry: stack/heap setup, main, exit
 * 0x213a  rt_exit                        Keil __rt_exit: lib shutdown then _sys_exit
 * 0x2148  rt_reset_handler               Reset vector: jumps to __main
 * 0x214c  rt_nmi_handler                 default NMI handler, infinite loop
 * 0x214e  rt_hardfault_handler           default HardFault handler, infinite loop
 * 0x2150  rt_svc_handler                 default SVCall handler, infinite loop
 * 0x2152  rt_pendsv_handler              default PendSV handler, infinite loop
 * 0x2156  rt_irq31_handler               default handler for IRQ31, infinite loop
 * 0x2158  rt_default_irq_handler         default handler for all unused IRQs, infinite loop
 * 0x215c  rt_user_initial_stackheap      returns heap 0x10001ac0..0x10001bc0, stack top 0x10001dc0
 * 0x217c  rt_memcmp                      
 * 0x220e  rt_memclr                      Keil __aeabi_memclr(p, n)
 * 0x2212  rt_memclr4                     Keil __aeabi_memclr4(p, n), word-aligned
 * 0x2216  rt_uidiv                       Keil __aeabi_uidivmod(num, den): unsigned, quotient r0, remainder r1
 * 0x222a  rt_idivmod                     
 * 0x2376  rt_user_setup_stackheap        Keil __user_setup_stackheap
 * 0x23b4  rt_exit_main                   Keil exit(): called with main's return value
 * 0x23c0  rt_user_libspace               returns C library work area 0x10001a5c
 * 0x23c8  rt_sys_exit                    Keil _sys_exit: semihosting bkpt 0xab then spin
 * 0x2410  tone_start                     program SCT0 to freq, 50% duty on PWM channel 2, set tone-on flag
 * 0x2444  tone_stop                      PWM channel 2 duty 0 and pin back to GPIO function
 * 0x2468  tone_tick                      tone pattern sequencer, called from the CT32B1 handler
 * 0x25a0  tone_play                      start tone pattern 0..3 unless state 6 or EEPROM 0x3e03 == 0xAA
 * 0x2654  fn_2654                        
 * 0x266c  uart0_send_frame_05_03         sends fixed USART0 frame AA 00 03 05 03 00 F5
 * 0x2694  uart0_addr_differs             compares two 6-byte addresses, 0 = equal
 * 0x26b8  uart0_dev_get_addr             copies own or record[idx] 6-byte address, returns 6 or 0
 * 0x2710  uart0_dev_get_name             copies own or record[idx] name (max 32 bytes), returns stored length
 * 0x27a8  get_uart0_dev_count            returns number of device records
 * 0x27b4  get_uart0_link_flag            returns flag set by a type-2 frame
 * 0x27c0  uart0_dev_clear_all            clears the 6 device records and the count
 * 0x2814  uart0_parse_frame              parses one 0xAA frame from USART0; -2 incomplete, -3 duplicate device, else -1
 * 0x29f4  uart0_own_name_clear           clears own-name buffer and length
 * 0x2a10  uart0_link_flag_clear          clears the type-2 flag
 * 0x2a1c  uart0_set_reply_mode           selects where the next name/address reply is stored
 * 0x2a28  uart0_dev_select               selects the record a name reply is stored into
 * 0x2a40  uart0_pins_release             P0_18/P0_19 to GPIO inputs
 * 0x2a78  uart0_pins_attach              P0_18/P0_19 to UART function and uart0_init(19200)
 * 0x2a94  uart0_task                     USART0 rx task: forward to USART2 in state 3, otherwise parse frame
 * 0x2ae0  i2c1_send_reply                appends XOR checksum to reply in I2C1 buffer and sets tx length
 * 0x2b28  cmd_frame_dispatch             wrapper passing a UART1/UART2 binary frame to uart_beef_frame_handler
 * 0x2b60  i2c1_buf_init                  stores I2C1 slave buffer address in i2c1_frame_ptr
 * 0x2b70  i2c1_cmd_task                  I2C1 slave command task with 93-case command switch
 * 0x30c0  cmd_power_off_fault            I2C1 case 24: power_off(2)
 * 0x30cc  cmd_gpio_read                  I2C1 case 9: reply level of port-0 pin given in arg byte
 * 0x3100  cmd_get_02e6                   I2C1 case 71: reply byte g_02e6 via fn_48fc
 * 0x312c  cmd_get_02e5                   I2C1 case 70: reply byte g_02e5 via fn_4908
 * 0x3158  cmd_get_uart4_flag             I2C1 case 74: reply uart4_flag, flag set/cleared by USART4 frames
 * 0x3184  cmd_get_uart4_data             I2C1 case 75: reply uart4_data_len bytes of uart4_data captured from a USART4 frame
 * 0x31c8  cmd_uart0_send_select          I2C1 case 92: record selection 1..3 and send frame AA 00 03 07 66 xx on USART0
 * 0x3260  cmd_get_0148                   I2C1 case 91: reply byte uart0_link_flag via get_uart0_link_flag
 * 0x328c  cmd_get_0146                   I2C1 case 89: reply byte uart0_dev_count via get_uart0_dev_count
 * 0x32b8  cmd_get_record                 I2C1 case 90: reply status + record from uart0_dev_get_name or uart0_dev_get_addr
 * 0x331c  cmd_get_p1_30                  I2C1 case 61: reply level of P1_30
 * 0x334c  cmd_get_p0_20                  I2C1 case 53: reply level of P0_20
 * 0x337c  cmd_get_p2_5_and_p2_14         I2C1 case 73: reply P2_5 AND P2_14
 * 0x33bc  cmd_get_fan_speeds             I2C1 case 32: reply five big-endian u16 speeds from averaged capture periods, 0 when stale
 * 0x368c  cmd_get_version                I2C1 case 1: reply "E01"
 * 0x36c8  cmd_get_status2                I2C1 case 22: reply 1 byte from uninitialised struct with bit 0 cleared
 * 0x3700  cmd_get_status                 I2C1 case 0: reply bit0 power_last_off_mode, bits1-3 power_state_code, bits4-7 wake_reason
 * 0x3770  power_off_normal               power_off(0)
 * 0x377c  cmd_get_const_61               I2C1 case 77: reply constant 0x61
 * 0x37a8  cmd_gpio_write                 I2C1 case 8: gpio_wr(arg0, arg1), port-0 pins only
 * 0x37c0  cmd_led_mode                   I2C1 case 26: led_mode(arg)
 * 0x37d4  cmd_set_p2_13                  I2C1 case 67: P2_13 = (arg == 1)
 * 0x37fc  cmd_set_p1_10                  I2C1 case 65: P1_10 = (arg == 1)
 * 0x3824  cmd_set_p0_11                  I2C1 case 76: P0_11 = (arg == 1)
 * 0x3848  cmd_call_78c4                  I2C1 case 63: uart1_send_2a20_frame(arg0, arg1)
 * 0x3860  cmd_call_7998                  I2C1 case 62: uart4_send_beef_frame(arg0, arg1)
 * 0x3878  cmd_uart0_send_record3         I2C1 case 85: send frame AA 00 07 03 + 6 bytes from uart0_dev_get_addr(0, arg) on USART0
 * 0x38cc  cmd_uart0_send_record2         I2C1 case 84: select source then send frame AA 00 07 02 + 6 bytes from uart0_dev_get_addr(b, a) on USART0
 * 0x394c  cmd_set_light_rails            I2C1 case 48: sequence P2_23/P1_15/P1_18 on (300 ms, 500 ms, 10 s, 10 s) or off, with USART0 up/down
 * 0x39e0  cmd_set_p1_29                  I2C1 case 51: P1_29 = (arg == 1)
 * 0x3a08  cmd_set_p0_17                  I2C1 case 58: P0_17 = (arg == 1)
 * 0x3a2c  cmd_set_p1_26_n                I2C1 case 59: P1_26 = !(arg == 1)
 * 0x3a54  cmd_call_4528                  I2C1 case 60: pulse_start(arg0, arg1, arg2, arg3)
 * 0x3a78  cmd_set_p1_21                  I2C1 case 56: P1_21 = (arg == 1)
 * 0x3aa0  cmd_set_p0_16                  I2C1 case 57: P0_16 = (arg == 1)
 * 0x3ac4  cmd_nop                        I2C1 case 50: empty stub
 * 0x3ac8  cmd_set_p0_22                  I2C1 case 55: P0_22 = (arg == 1)
 * 0x3aec  cmd_set_p1_8_n                 I2C1 case 54: P1_8 = !(arg == 1), request remembered in cmd_p1_8_asserted
 * 0x3b24  cmd_set_sel_p1_12_13           I2C1 case 52: gpio_sel_p1_13_12(arg), 2-bit selector
 * 0x3b38  cmd_set_p1_19                  I2C1 case 69: P1_19 = (arg == 1)
 * 0x3b60  cmd_set_p1_17                  I2C1 case 66: P1_17 = (arg == 1)
 * 0x3b88  cmd_set_p0_21_n                I2C1 case 72: P0_21 = !(arg == 1)
 * 0x3bac  cmd_set_p1_2                   I2C1 case 64: P1_2 = (arg == 1)
 * 0x3bd4  cmd_set_p0_8                   I2C1 case 68: P0_8 = (arg == 1)
 * 0x3bf8  cmd_set_p2_17_n                I2C1 case 49: P2_17 = !(arg == 1)
 * 0x3c20  uart_beef_frame_handler        BE EF 10 frame handler for USART1/2: class 0x11 -> cmd_binary_ack(value), class 0x22 -> cmd_exec_code(value)
 * 0x3cf0  led_mode                       
 * 0x3fc8  uart4_beef_frame_handler       BE EF 10 frame handler for USART4: sets uart4_flag, captures data to uart4_data
 * 0x40c0  gpio_dir_clear                 clears the pin's bit in LPC_GPIO->DIR[port] (make input)
 * 0x40e8  iocon_cfg                      
 * 0x4230  gpio_port_write                raw GPIO write (port, pin, level): sets DIR bit then SET/CLR
 * 0x4280  gpio_port_read                 raw GPIO read (port, pin) from PIN register, returns 0/1
 * 0x42a8  power_sleep_wfi                writes PDSLEEPCFG, optionally sets SLEEPDEEP, then wfi
 * 0x42c8  uart0_send_blocking            USART0 polled transmit (buf, len)
 * 0x4304  uart1_send_blocking            USART1 polled transmit (buf, len)
 * 0x433c  uart2_send_blocking            USART2 polled transmit (buf, len)
 * 0x4374  uart4_send_blocking            USART4 polled transmit (buf, len)
 * 0x43ac  setting_get_s2                 copies settings field 2 (0x10001a4b) to *out
 * 0x43b8  setting_get_mac                copies the 6-byte MAC from the settings block
 * 0x43d4  setting_classify_1a0c          returns 0x55 for {1,5,5,0}, 0xAA for {1,6,5,0} in g_1a0c, else the argument
 * 0x4420  setting_get_standby_mode       copies settings.standby_mode to *out
 * 0x442c  setting_get_s4                 copies settings field 4 (0x10001a4d) to *out
 * 0x4438  setting_get_1a43               copies byte 0x10001a43 to *out
 * 0x4444  setting_get_wol_enable         copies settings.wol_enable to *out
 * 0x4450  setting_get_1a18               copies u32 0x10001a18 to *out
 * 0x445c  load_settings                  
 * 0x4508  setting_set_wol_enable         stores WoL enable in RAM and writes it to its EEPROM cell
 * 0x4528  pulse_start                    arms the P1_3/P1_4 pulse generator (dir, on*10, off*10, count) run by gpio_pulse_tick
 * 0x4574  gint0_irq                      GINT0 handler, acknowledge only
 * 0x4598  gint1_irq                      GINT1 handler, acknowledge only
 * 0x45bc  pint_ack                       clears RISE/FALL and IST for an edge pin-interrupt channel
 * 0x4608  pint_disable                   disables falling (arg 0) or rising edge interrupt for a channel
 * 0x4650  pint_enable                    enables falling (arg 0) or rising edge interrupt for a channel
 * 0x4698  pint_pending                   returns 1 if IST bit of the channel is set
 * 0x46b4  gpio_as_input                  
 * 0x4780  gpio_rd                        
 * 0x47a0  gpio_init_table                applies 80-entry pin table rom_96b4 when arg != 0, then P2_17 = 0
 * 0x485c  gpio_dir                       
 * 0x48ba  gpio_wr                        
 * 0x48d8  cmd_xor_checksum               XOR of seed with first len bytes of the frame at *i2c1_frame_ptr
 * 0x48fc  fn_48fc                        
 * 0x4908  fn_4908                        
 * 0x4914  fn_4914                        
 * 0x4920  fn_4920                        
 * 0x492c  eeprom_ac_ok                   
 * 0x4938  i2c0_buf_ptr_init              sets i2c0_buf_ptr to the I2C0 buffer address
 * 0x4948  i2c0_rx_count_clear            zeroes the I2C0 slave receive count
 * 0x4954  get_i2c0_buf                   returns the I2C0 buffer address (0x10000596)
 * 0x495c  get_i2c0_rx_count              returns the I2C0 slave receive count
 * 0x4968  i2c0_irq                       I2C0 interrupt, combined master/slave state machine
 * 0x4c80  i2c0_cmd_task                  handles 2-byte I2C0 slave commands 3..6 (selector, led_mode, off sequence, power_on)
 * 0x4d44  i2c1_master_start              resets I2C1 master state and sets STA, returns 1
 * 0x4d6c  i2c1_rx_count_clear            zeroes the I2C1 slave receive count
 * 0x4d78  get_i2c1_buf                   returns the I2C1 buffer address (0x100005ba)
 * 0x4d80  get_i2c1_rx_count              returns the I2C1 slave receive count
 * 0x4d8c  i2c1_own_addr                  returns low byte of I2C1 ADR0
 * 0x4d98  i2c1_irq                       I2C1 interrupt, combined master/slave state machine (EEPROM master + command slave)
 * 0x504e  fn_504e                        
 * 0x5070  i2c1_master_write              sends sla + len data bytes on I2C1, waits 200 ms, returns 1 ok / 3 NACK
 * 0x50d4  i2c1_master_write_read         I2C1 master: write wlen bytes then read rlen bytes via ISR buffer i2c1_buf (used by eeprom_read_byte)
 * 0x5178  i2c1_stub_nop                  empty stub
 * 0x517c  i2c1_set_tx_len                sets I2C1 transmit length i2c1_tx_len with IRQ10 masked
 * 0x5198  ir_take_frame_done             take-and-clear the 0xff "complete frame ended" marker
 * 0x51d0  ir_take_code                   
 * 0x520c  get_ir_timeout_ticks           returns IR inter-edge timeout (147 ticks)
 * 0x5210  ir_on_timeout                  timer IRQ: no edge for >147 ticks, end reception and flag idle
 * 0x524c  ir_on_edge                     PINT3 falling edge: NEC bit decode, address 0x504f or 0xffff
 * 0x53f4  ir_init                        routes P1_1 to PINT3 falling edge and clears decoder state
 * 0x5448  ir_idle_flag                   returns (optionally clears) the IR timeout/idle flag
 * 0x5484  gpio_pint_config               configure pin interrupt n for port/pin, edge or level, polarity
 * 0x56a4  i2c0_init                      I2C0 on P0_4/P0_5, own address 0x66, IRQ15
 * 0x5730  i2c1_init                      I2C1 on P1_11/P1_14, own address 0xb0, IRQ10
 * 0x57c0  clock_init                     crystal osc + PLL (x4 -> 48 MHz) + main clock select + AHB divider
 * 0x588c  uart0_init                     
 * 0x595c  uart1_init                     
 * 0x5a24  uart2_init                     
 * 0x5af4  uart3_init                     
 * 0x5bbc  uart4_init                     
 * 0x5c84  key_wait_power_loop            blocking loop servicing UART tasks until power key/IR power seen 10 times or wait_loop_exit set
 * 0x5d18  key_take_power_event           take-and-clear power-key event flag
 * 0x5d2c  key_reset                      clears power-key event and latched press
 * 0x5d40  key_power_debounce             power key P1_16 debounce, event on release after >4 passes held
 * 0x5de4  led_set_pattern                program LED pattern: two LED indexes, two phase times, alternate flag
 * 0x5e18  led_tick                       timer IRQ LED engine: blink/alternate GPIO LEDs or breathe via PWM
 * 0x5fd0  led_all_off                    drives P2_10, P2_9, P2_2, P2_8, P2_6, P2_18 low
 * 0x6014  led_set_pair                   stores LED indexes a/b
 * 0x6028  led_set_times                  stores phase 1/2 durations
 * 0x603c  led_set_alt                    stores alternate flag
 * 0x6048  led_write                      drive LED by index: 0=P2_2 1=P2_8 2=P2_9 3=P2_10 4=P2_6 5=none
 * 0x60a0  uart1_task                     USART1 rx task: needs >=12 bytes, hands frame to cmd_frame_dispatch, 30000-tick partial-frame timeout
 * 0x613c  cmd_beef_check_wrap            thin wrapper around BE-EF frame checker uart4_beef_frame_handler(len, buf, flag)
 * 0x6150  uart4_task                     main-loop task for the RX ring uart4_rx_count: bridge to 0x40070000 UART in state 3, else BE-EF frame reassembly
 * 0x62b0  gpio_pulse_tick                SysTick-driven pulse train on P1_3/P1_4 (count, on-time, gap, polarity)
 * 0x63b8  nvic_disable_irq_a             NVIC ICER write (CMSIS copy)
 * 0x63cc  nvic_disable_irq               NVIC ICER write (copy used by UART modules)
 * 0x63e0  nvic_enable_irq_a              NVIC ISER write (CMSIS copy)
 * 0x63f4  nvic_enable_irq_b              NVIC ISER write (CMSIS copy)
 * 0x6408  nvic_enable_irq_c              NVIC ISER write (CMSIS copy, timers)
 * 0x641c  nvic_enable_irq                NVIC ISER write (copy used by UART modules)
 * 0x6430  nvic_set_priority              CMSIS NVIC_SetPriority(irq, prio)
 * 0x64a8  eeprom_select_a4               if eeprom_probed == 0: EEPROM device 0xa4, geometry index 5
 * 0x64c8  eeprom_read                    
 * 0x64f4  eeprom_read_byte               
 * 0x6524  eeprom_set_type                sets eeprom_type_idx
 * 0x6530  eeprom_write                   
 * 0x655c  eeprom_write_byte              
 * 0x6590  cmd_exec_code                  handles command codes 0x08/0x4e/0xd1/0xd2 and sends reply frame via uart1_send_beef_status
 * 0x65e8  phy_wol_state_get              returns phy_wol_state
 * 0x65f4  phy_leave_wol                  
 * 0x6614  phy_wol_state_set              sets phy_wol_state
 * 0x6620  phy_arm_wol                    
 * 0x6734  phy_pme_fell                   
 * 0x6780  pint0_irq                      PIN_INT0 handler, acknowledge only
 * 0x67cc  pint1_irq                      PIN_INT1 handler, acknowledge only
 * 0x6814  pint2_irq                      PIN_INT2 handler, empty
 * 0x6816  pint3_irq                      PIN_INT3 handler, runs ir_on_edge(0) pulse measurement then re-arms falling edge
 * 0x6840  pint4_irq                      PIN_INT4 handler, acknowledge only
 * 0x6888  pint5_irq                      PIN_INT5 handler, acknowledge only
 * 0x68d0  pint6_irq                      PIN_INT6 handler, acknowledge only
 * 0x6918  pint7_irq                      PIN_INT7 handler, acknowledge only
 * 0x6960  power_standby_pins             standby pin teardown: pins back to GPIO, P2_11/P2_12 inputs, main-side outputs off
 * 0x6a04  cmd_ascii_reply                sends "P\r", "P<val>\r" or "F\r" on the 0x40070000 UART
 * 0x6a68  cmd_binary_ack                 sends 2/3-byte ack for a command code; code 1 requests power-on, 0x6d sets wait_loop_exit
 * 0x6adc  uart_wake_token_take           returns uart_wake_token and re-arms it to 0x14
 * 0x6af0  uart_tasks_init                caches the four UART RX buffer pointers, resets wake token and task flags
 * 0x6b48  uart2_task                     main-loop task for RX ring uart2_rx_count: bridge in state 3, ASCII '#' commands, else binary frames
 * 0x6cf4  sct0_irq                       SCT0 interrupt, clears EVFLAG values 1..3
 * 0x6d3c  pwm_init                       SCT0 PWM time base, period = 48 MHz / (arg * 5)
 * 0x6e00  pwm_set                        enable PWM channel 0..3 at duty percent and start SCT0
 * 0x6fc0  power_on_late_task             30000 ticks after power-on: P1_24 = 1, uart4_init; then P1_8/P0_22 follow P0_20
 * 0x7050  standby_settled                
 * 0x7094  power_fault_monitor            on-state monitor of P1_27; 35000 ticks low -> fault code 2, LED mode 9, power_off(2)
 * 0x7148  eeprom_read_checked_3e03       returns EEPROM byte 0x3e03 if byte 0x3e04 is its complement, else 0
 * 0x7178  setting_s2_is_zero             returns 1 if settings.s2 (0x10001a4b) == 0
 * 0x7198  power_get_flag_02f7            returns g_02f7
 * 0x71a4  setting_keep_rails             
 * 0x71c4  power_get_is_on                returns power_is_on
 * 0x71d0  setting_s4_is_zero             returns 1 if settings.s4 (0x10001a4d) == 0; used by led_mode
 * 0x71f0  power_get_wake_reason          returns wake_reason
 * 0x71fc  power_get_flag_02f1            returns g_02f1
 * 0x7208  power_get_flag_02f2            returns g_02f2
 * 0x7214  setting_wol_enabled            
 * 0x7234  power_get_last_off_mode        returns mode argument of the last power_off()
 * 0x7240  power_get_state_code           returns power_state_code
 * 0x724c  power_clear_on_flags           clears power_is_on, g_02e6, g_02e5, factory_test10_active, factory_test3_active (no GPIO)
 * 0x7278  power_on                       
 * 0x7364  power_set_wake_reason          wake_reason = arg
 * 0x7370  power_off                      
 * 0x7730  eeprom_flag_bit_write          set/clear one bit of EEPROM byte 0x3e00 (read-modify-write)
 * 0x776e  gpio_sel_p1_23_22              2-bit selector, P1_23 = bit1, P1_22 = bit0
 * 0x77e0  gpio_sel_p2_21_20_n            2-bit selector, active low: P2_21 = !bit1 (and bit0 pattern inverted), 0 -> both high, 3 -> both low
 * 0x7850  gpio_sel_p1_13_12              2-bit selector, P1_13 = bit1, P1_12 = bit0
 * 0x78c4  uart1_send_2a20_frame          builds "2A 20" additive-checksum frame (cmd 1..15, arg) and sends on USART1 after 200 ticks
 * 0x7998  uart4_send_beef_frame          builds "BE EF" CRC16 frame (cmd 1..17, arg) and sends on USART4 after 200 ticks
 * 0x7a48  uart1_send_beef_status         builds "BE EF" CRC16 frame for cmd 0xD1/0xD2/0xD5/0xDA, always sends 40 bytes on USART1
 * 0x7bbc  power_set_flag_02e6            g_02e6 = arg
 * 0x7bc8  power_set_flag_02e5            g_02e5 = arg
 * 0x7bd4  timer_systick_isr              SysTick: tick++, 8 soft timers++, two down-counters
 * 0x7c34  timer_ct16b1_isr               CT16B1 MR0: timeout counter, /100 and /10000 counters
 * 0x7d0c  timer_ct32b0_isr               CT32B0 MR0 overflow count + CAP0 edge-interval logging of P2_9
 * 0x7e48  timer_ct32b1_isr               CT32B1 MR0: ct32b1_tick_count++, led_tick(ct32b1_tick_count), optional tone_tick, key_hold_ticks++
 * 0x7e98  timer_timeout_reset            ir_tick[idx] = 0
 * 0x7ec0  timer_soft0_reset              timer_soft[0] = 0
 * 0x7ee0  timer_soft1_get                returns timer_soft[1]
 * 0x7eec  timer_soft2_get                returns timer_soft[2] (standby-settle timer)
 * 0x7ef8  timer_soft4_get                returns timer_soft[4]
 * 0x7f04  timer_soft5_get                returns timer_soft[5]
 * 0x7f10  timer_soft3_get                returns timer_soft[3]
 * 0x7f1c  timer_timeout_get              returns ir_tick[idx]
 * 0x7f2c  timer_soft0_get16              returns low 16 bits of timer_soft[0]
 * 0x7f38  timer_systick_init             zero tick state, SysTick_Config(4800), priority 3
 * 0x7fc4  timer_soft1_reset              timer_soft[1] = 0
 * 0x7fe4  timer_soft2_reset              timer_soft[2] = 0 (restart standby-settle timer)
 * 0x8004  timer_soft4_reset              timer_soft[4] = 0
 * 0x8024  timer_soft5_reset              timer_soft[5] = 0
 * 0x8044  timer_soft3_reset              timer_soft[3] = 0
 * 0x8064  timer_hw_setup                 0 = CT32B1 (PR 47, MR0 49999, IRQ19), 1 = CT16B1 (PR 47, MR0 99, IRQ17), 3 = CT32B0 MR1/MR3 36418 no IRQ
 * 0x8120  timer_ct32b1_count_reset       ct32b1_tick_count = 0
 * 0x812c  rt_nop                         empty function
 * 0x8130  delay_ticks                    
 * 0x8158  uart0_rx_reset                 clears USART0 rx count/flag with IRQ 21 masked
 * 0x817c  get_uart0_rx_buf               returns USART0 rx buffer pointer (0x100018c0)
 * 0x8184  get_uart0_rx_count             returns USART0 rx count
 * 0x8190  uart0_send                     wrapper for USART0 blocking transmit uart0_send_blocking(buf, len)
 * 0x81a0  uart1_rx_reset                 clears USART1 rx count/flag with IRQ 11 masked
 * 0x81c4  get_uart1_rx_buf               returns USART1 rx buffer pointer (0x10001900)
 * 0x81cc  get_uart1_rx_count             returns USART1 rx count
 * 0x81d8  get_uart1_rx_flag              returns uart1_rx_flag
 * 0x81e4  uart1_send                     wrapper for USART1 blocking transmit uart1_send_blocking(buf, len)
 * 0x81f4  uart2_rx_reset                 clears USART2 rx count/flag with IRQ 12 masked
 * 0x8218  get_uart2_rx_buf               returns USART2 rx buffer pointer (0x10001940)
 * 0x8220  get_uart2_rx_count             returns USART2 rx count
 * 0x822c  get_uart2_rx_flag              returns uart2_rx_flag
 * 0x8238  uart2_send                     wrapper for USART2 blocking transmit uart2_send_blocking(buf, len)
 * 0x8248  uart4_rx_reset                 clears USART4 rx count/flag with IRQ 11 masked
 * 0x826c  get_uart4_rx_buf               returns pointer to USART4 receive buffer (0x100019c0)
 * 0x8274  get_uart4_rx_count             returns USART4 receive byte count / write index
 * 0x8280  fn_8280                        
 * 0x828c  fn_828c                        
 * 0x829c  uart0_irq                      USART0 IRQ: LF-terminated line receive into 0x100018c0, THRE flag
 * 0x833c  uart1_4_irq                    shared USART1/USART4 IRQ: 64-byte wrapping receive, 'P' (0x50) detect, TX-ready flag
 * 0x8448  uart2_3_irq                    shared USART2/USART3 IRQ: same as uart1_4_irq
 * 0x8554  eeprom_reload_factory_mode     wrapper for 0x87d8
 * 0x855c  fn_855c                        
 * 0x8564  power_on_rails                 
 * 0x87d8  eeprom_read_factory_mode       reads EEPROM cell 0x3e06 into 0x100002f3
 * 0x87ec  rt_switch8                     
 * 0x8808  eeprom_addr_split              linear address -> I2C device byte + 1/2 address bytes using geometry table 0x9934; 0 if out of range
 * 0x88d8  main                           
 * 0x8fcc  mdio_op_read                   shifts out opcode bits 1,0
 * 0x9048  mdio_op_write                  shifts out opcode bits 0,1
 * 0x90c4  mdio_out5                      shifts out a 5-bit field MSB first (PHY address / register)
 * 0x912c  mdio_idle_bit                  one clock with data driven high after a frame
 * 0x916c  mdio_read                      
 * 0x9248  mdio_start                     shifts out start-of-frame bits 0,1
 * 0x92c4  mdio_turnaround                shifts out turnaround bits 1,0
 * 0x9340  mdio_preamble                  32 clock pulses
 * 0x9388  mdio_write                     
 * 0x9428  fn_9428                        
 * 0x944c  crc16_modbus                   table-driven CRC-16/Modbus over (ptr, u8 len), returns (hi<<8)|lo
 */

/* ---- RAM globals (address, name, type, summary) ----
 * 0x10000100  fan_speed_last             u16[5] last reported speed per channel (0x100..0x108)
 * 0x1000010a  fan_speed_same_count       u8[5]  consecutive polls with unchanged speed per channel (0x10a..0x10e)
 * 0x1000010f  cmd_p1_8_asserted          u8     1 while I2C1 case 54 holds P1_8 low
 * 0x10000110  i2c1_frame_ptr             u8*    pointer to I2C1 slave rx/tx buffer (0x100005ba)
 * 0x10000114  i2c1_reply_fixed_seed      u8     non-zero: reply XOR seed is 0x50 instead of own address | 1; cleared on each 0xF1 command
 * 0x10000116  factory_test_armed         u8     set once key_hold_ticks reached 60 (led_mode 0x12 shown)
 * 0x10000118  factory_test3_active       u8     set by factory test IR code 3; with 0x0119 suppresses P2_17 in power_on_rails
 * 0x10000119  factory_test10_active      u8     set by factory test IR code 0x10; with 0x0118 suppresses P2_17 in power_on_rails
 * 0x1000011c  uart_frame_class           u8     0 = last BE EF frame was class 0x11, 1 = class 0x22
 * 0x10000120  cmd_ascii_table            u8*[3] pointers to "#00500 0", "#00500 1", "#00501 1" (rom_9958..)
 * 0x1000012c  uart_wake_token                   
 * 0x10000130  uart2_rx_buf_ptr           u8*    RX buffer pointer used by task 0x6b48 (0x10001940)
 * 0x10000134  uart0_rx_buf_ptr           u8*    pointer to USART0 receive buffer
 * 0x10000138  uart1_rx_buf_ptr           u8*    pointer to the USART1 receive buffer
 * 0x1000013c  uart4_rx_buf_ptr           u8*    RX buffer pointer used by task 0x6150 (0x100019c0)
 * 0x10000143  uart2_partial_timing       u8     1 = timeout timer running for a short (<12 byte) binary frame
 * 0x10000144  uart1_partial_pending      u8     1 = short frame pending, timeout running
 * 0x10000145  uart4_partial_timing       u8     1 = timeout timer running for a short (<12 byte) frame
 * 0x10000146  uart0_dev_count            u8     number of device records in use (max 6)
 * 0x10000147  uart0_dev_sel              u8     record index that receives a type-5 name
 * 0x10000148  uart0_link_flag            u8     1 when last type-2 frame carried 0 or 2
 * 0x10000149  uart0_reply_mode           u8     0 = name to selected record, 1..3 = own address byte pair / own name
 * 0x1000014a  uart0_own_addr             u8[6]  own 6-byte address, filled by type-0xFF frames
 * 0x10000150  uart0_own_name_len         u8     length of own name
 * 0x10000151  uart4_flag                 u8     set by USART4 frame sub 1 value 7, cleared by sub 2 value 7
 * 0x10000152  uart4_data_len             u8     number of bytes held in uart4_data
 * 0x10000153  uart2_ascii_line           u8     1 = line started with '#', ASCII command in progress
 * 0x10000154  uart4_second_frame         u8     1 = first frame parsed, waiting for the following frame
 * 0x10000155  uart4_frame_flag           u8     passed to the BE-EF checker; set when header byte 11 == 7
 * 0x10000156  uart4_first_len            u8     length of first frame (12 or 13) = offset of the second frame
 * 0x10000158  pulse_state                u8     pulse generator: 0 idle, 1 start, 2.. running
 * 0x1000015a  pulse_ticks                u16    pulse generator tick counter
 * 0x1000015c  pulse_dir                  u8     0 = P1_3 high / P1_4 low, else the reverse
 * 0x1000015e  pulse_on_ticks             u16    on time, argument * 10
 * 0x10000160  pulse_off_ticks            u16    off time, argument * 10
 * 0x10000162  pulse_done                 u8     pulses completed
 * 0x10000163  pulse_count                u8     pulses requested
 * 0x10000164  i2c0_master_state          u32    0 idle, 1 START, 2 rep-START, 4 busy, 5 done/aborted
 * 0x10000168  i2c0_slave_state           u32    0 idle, 6 receiving, 7 transmitting, 5 NACKed
 * 0x1000016c  i2c1_master_state          u32    0 idle, 1 START, 2 rep-START, 4 busy, 5 done/aborted
 * 0x10000170  i2c1_slave_state           u32    0 idle, 6 receiving, 7 transmitting, 5 NACKed
 * 0x10000174  i2c0_idx                   u8     I2C0 buffer index
 * 0x10000175  i2c0_rx_count              u8     bytes received as slave (also master read length)
 * 0x10000176  i2c0_tx_len                u8     number of bytes to transmit
 * 0x10000177  i2c1_idx                   u8     I2C1 buffer index
 * 0x10000178  i2c1_rx_count              u8     bytes received as slave (also master read length)
 * 0x10000179  i2c1_tx_len                u8     number of bytes to transmit
 * 0x1000017a  i2c0_irq_seen              u8     set to 1 on every I2C0 interrupt
 * 0x1000017b  i2c0_nack                  u8     set when an I2C0 master transfer is NACKed
 * 0x1000017c  i2c1_irq_seen              u8     set to 1 on every I2C1 interrupt
 * 0x1000017d  i2c1_error                 u8     set when an I2C1 master transfer is NACKed
 * 0x10000180  i2c0_buf_ptr               u8*    pointer to i2c0_buf used by the command task
 * 0x10000184  ir_shift                   u16[1] IR shift register, LSB first
 * 0x10000186  ir_code                    u8[1]  decoded IR command byte, 0 = none
 * 0x10000187  ir_frame_done              u8[1]  0xff after a complete 32-bit frame timed out
 * 0x10000188  ir_interval                u16[1] ticks between the last two falling edges
 * 0x1000018a  ir_bit_count               u8[1]  bits received in current frame (+1 after leader)
 * 0x1000018b  ir_receiving               u8[1]  1 = leader seen
 * 0x1000018c  ir_idle                    u8[1]  1 = inter-frame timeout occurred
 * 0x1000018d  ir_addr_is_ffff            u8     1 = address word was 0xffff rather than 0x504f
 * 0x1000018e  key_power_event            u8     1 = power key pressed and released (one-shot)
 * 0x1000018f  key_power_latched          u8     1 = press latched, waiting for release
 * 0x10000190  key_power_count            u8     passes counted while held
 * 0x10000191  key_power_state            u8     0 = counting, 1 = qualified, 2 = idle
 * 0x10000192  led_pwm_duty               u8     last PWM duty written for breathing (0..100)
 * 0x10000193  led_time1                  u8     phase 1 duration in ticks
 * 0x10000194  led_time2                  u8     phase 2 duration in ticks
 * 0x10000195  led_a                      u8     first LED index of the pattern
 * 0x10000196  led_b                      u8     second LED index of the pattern
 * 0x10000197  led_phase                  u8     set to 1 by several led_mode cases
 * 0x10000198  led_alternate              u8     1 = LEDs a and b alternate
 * 0x10000199  led_mode_current           u8     last mode applied by led_mode
 * 0x1000019c  timer_tick_count           u32    free-running SysTick count
 * 0x100001a0  ir_tick                    u16[1] ticks since last IR edge (timer IRQ increments)
 * 0x100001a4  delay_counter                     
 * 0x100001a8  timer_down_counter         u32    decremented to 0 by SysTick
 * 0x100001ac  ir_timeout_ticks           u16    IR timeout in ticks (147)
 * 0x100001ae  timer_ct16b1_div100        u8     CT16B1 prescaler 0..99
 * 0x100001af  timer_ct16b1_div10000      u8     second-stage prescaler 0..99
 * 0x100001b0  ct32b1_tick_count          u16    LED phase tick counter
 * 0x100001b4  key_hold_ticks             u16    second CT32B1 interrupt count
 * 0x100001e8  ct32b0_tc_at_low           u32    CT32B0 TC sampled when P2_9 read low
 * 0x100001ec  ct32b0_tc_at_high          u32    CT32B0 TC sampled when P2_9 read high
 * 0x100001f0  ct32b0_high_width          u32    last interval ending with P2_9 low
 * 0x100001f4  ct32b0_low_width           u32    last interval ending with P2_9 high
 * 0x10000218  ct32b0_overflow_count      u32    CT32B0 MR0 overflows since last capture edge
 * 0x10000238  uart0_tx_empty             u8     set to 1 by uart0_init
 * 0x1000023c  uart0_rx_count             u32    USART0 bytes received
 * 0x10000240  uart0_rx_line_done         u8     set to 1 when 0x0A received
 * 0x10000241  uart0_rx_activity          u8     set to 1 on every received byte
 * 0x10000244  uart1_tx_ready             u8     set in TXRDY interrupt
 * 0x10000245  uart1_rx_wrapped           u8     set when index wrapped at 0x40
 * 0x10000246  uart1_rx_got_p             u8     set when byte 0x50 received (byte stored as 0)
 * 0x10000248  uart1_rx_count             u32    USART1 bytes received
 * 0x1000024c  uart1_rx_flag              u8     USART1 rx flag that forces a flush (error/overflow ?)
 * 0x1000024d  uart2_tx_ready             u8     set in TXRDY interrupt
 * 0x1000024e  uart2_rx_wrapped           u8     set when index wrapped at 0x40
 * 0x1000024f  uart2_rx_got_p             u8     set when byte 0x50 received (byte stored as 0)
 * 0x10000250  uart2_rx_count             u32    USART2 received byte count
 * 0x10000254  uart2_rx_flag              u8     USART2 rx state byte, cleared with the count
 * 0x10000255  uart3_tx_ready             u8     set in TXRDY interrupt
 * 0x10000256  uart3_rx_wrapped           u8     set when index wrapped at 0x40
 * 0x10000257  uart3_rx_got_p             u8     set when byte 0x50 received (byte stored as 0)
 * 0x10000258  uart3_rx_count             u32    USART3 receive index, wraps at 0x40
 * 0x1000025d  uart4_tx_ready             u8     set in TXRDY interrupt
 * 0x1000025e  uart4_rx_wrapped           u8     set when index wrapped at 0x40
 * 0x1000025f  uart4_rx_got_p             u8     set when byte 0x50 received (byte stored as 0)
 * 0x10000260  uart4_rx_count             u32    USART4 received byte count
 * 0x10000264  uart4_rx_flag              u8     USART4 rx state byte, cleared with the count
 * 0x10000268  pwm_period                 u32    SCT0 match value for the PWM period
 * 0x1000026c  mdio_shift_reg             u32    MDIO read accumulator
 * 0x10000270  mdio_bit_count             u8     loop counter shared by all MDIO primitives
 * 0x10000271  tone_on                    u8     1 while a tone is sounding, 0 when idle
 * 0x10000272  tone_inhibit_flag          u8     when set, tone_start leaves tone_on at 0; cleared by tone_play
 * 0x10000273  tone_pattern               u8     running tone pattern 0 = idle, 1..4
 * 0x10000274  tone_step                  u8     step inside the running pattern
 * 0x10000275  tone_ticks                 u8     timer ticks spent in the current step
 * 0x10000276  eeprom_i2c_addr                   
 * 0x10000277  eeprom_type_idx                   
 * 0x10000278  eeprom_ac_present                 
 * 0x10000279  eeprom_probed              u8     1 after load_settings has probed the EEPROM address once
 * 0x100002a8  timer_slow_count           u32    ++ every 10000 CT16B1 interrupts
 * 0x100002ac  timer_cnt100_c             u32    ++ every 100 CT16B1 interrupts
 * 0x100002b0  timer_cnt100_d             u32    ++ every 100 CT16B1 interrupts
 * 0x100002b8  timer_cnt100_b             u32    ++ every 100 CT16B1 interrupts
 * 0x100002ce  cmd_81_seen                u8     set to 1 by I2C command 81
 * 0x100002cf  cmd_80_seen                u8     set to 1 by I2C command 80
 * 0x100002dc  timer_cnt100_a             u32    ++ every 100 CT16B1 interrupts
 * 0x100002e1  phy_wol_state                     
 * 0x100002e2  phy_pme_last               u8     last sampled level of P1_28
 * 0x100002e3  led_fault_latch            u8     1..9 once a fault LED mode (9..17) is shown; blocks further led_mode calls
 * 0x100002e4  wait_loop_exit             u8     set elsewhere to break out of key_wait_power_loop
 * 0x100002e7  power_last_off_mode        u8     mode argument of the last power_off()
 * 0x100002e8  power_is_on                       
 * 0x100002e9  power_state_code                  
 * 0x100002ea  wake_reason                       
 * 0x100002eb  power_fault_armed          u8     1 once the fault monitor's 15000-tick start delay has elapsed
 * 0x100002ec  power_fault_code           u8     set to 2 when P1_27 fault shutdown triggers
 * 0x100002f3  factory_mode               u8     mirror of EEPROM cell 0x3e06; set 1 by IR code 8, 0 by IR code 0xb in the key-held test path
 * 0x100002f4  power_on_late_done         u8     1 after the 30000-tick late bring-up (P1_24, uart4_init) has run
 * 0x100002f6  standby_settled_flag              
 * 0x100002f8  standby_rails_up                  
 * 0x100002fc  uart_tx_buf                u8[]   reply/transmit scratch buffer
 * 0x1000039c  uart0_own_name             u8[64] own name buffer
 * 0x100003dc  uart4_data                 u8[]   payload captured from a USART4 frame, read back by I2C1 case 75
 * 0x100003e6  uart0_dev_table            struct[6] 0x48-byte records: addr[6], kind, name[64], name_len
 * 0x10000596  i2c0_buf                   u8[36] I2C0 tx/rx buffer, [0] is SLA in master mode or command in slave mode
 * 0x100005ba  i2c1_buf                   u8[]   I2C1 tx/rx buffer, [0] is SLA in master mode
 * 0x100005e0  timer_soft                 u32[]  software tick timers; [0] fault, [1] since power-on, [2] since standby, [3] uart2, [5] uart4
 * 0x10000790  fan_capture0               u32[100] capture block for channel 0 (words 10,20,30,40,50 are averaged)
 * 0x10000920  fan_capture1               u32[100] capture block for channel 1
 * 0x10000ab0  ct32b0_high_log            u32[100] capture block for channel 2
 * 0x10000c40  fan_capture3               u32[100] capture block for channel 3
 * 0x10000dd0  fan_capture4               u32[100] capture block for channel 4
 * 0x10001410  ct32b0_low_log             u32[99] circular log of ct32b0_low_width, index at 0x10000235
 * 0x100018c0  uart0_rx_buf               u8[]   USART0 receive buffer
 * 0x10001900  uart1_rx_buf               u8[]   USART1 receive buffer
 * 0x10001940  uart2_rx_buf               u8[]   USART2 receive buffer
 * 0x10001980  uart3_rx_buf               u8[64] USART3 receive buffer
 * 0x100019c0  uart4_rx_buf               u8[64] USART4 receive buffer
 * 0x10001a00  settings_ptrs                     
 */


/* ======================================================================
 * Part 1
 * ====================================================================== */

/*
 * part_1.c -- LPC11U67 application, 0x20c0 <= addr < 0x30c0
 *
 *   0x20c0-0x240f  Keil/ARMCC startup + runtime library
 *   0x2410-0x2653  tone generator (SCT0 PWM) + tone sequencer
 *   0x2654-0x2ade  USART0 binary frame link (0xAA frames): device table,
 *                  parser, pin release/attach, task
 *   0x2ae0-0x30bf  I2C1 slave: reply finaliser, frame task with the
 *                  93-case command switch
 *
 * Conventions used below
 *   i2c  = i2c1_frame_ptr            (u8 *, points at the I2C1 slave buffer 0x100005ba)
 *   rec  = uart0_dev_table[i]         6 records of 0x48 bytes at 0x100003e6:
 *            +0x00 u8 addr[6]
 *            +0x06 u8 kind
 *            +0x07 u8 name[0x40]
 *            +0x47 u8 name_len
 *   USART0 frame: AA, len_hi, len_lo, type, data[len-1], cksum
 *            total bytes = len + 4; cksum = (u8)(0x100 - sum(bytes 1..len+2))
 *   I2C1 frame:  id, n, payload[n & 0x7f], xor
 *            total bytes = (n & 0x7f) + 3
 *            rx xor seed = own slave address (I2C1 ADR0)
 *            id 0xF1 = command, payload[0] (= buf[2]) = command byte
 */

/* ===================================================================== */
/* Startup / runtime library                                             */
/* ===================================================================== */

/* 0x20c0  callers: 0 (entered from the reset handler rt_reset_handler) */
/* Keil __main: scatter-load .data/.bss, then enter the C library start-up.
 * Neither call returns. */
void rt_main(void)
{
    rt_scatterload();
    rt_entry();
}

/* 0x20c8  callers: 1 */
/* Keil __scatterload. Walks the region table rom_997c .. rom_999c (the two
 * words at 0x20fc/0x2100 are the pc-relative offsets 0x7880/0x78a0 of its
 * start and end). Each 16-byte entry is { src, dst, len, handler } and the
 * handler is tail-called with r0=src r1=dst r2=len, returning into this loop.
 *   entry 0: src 0x999c, dst 0x10000100, len 0x1fc,  handler 0x23d6 (decompress)
 *   entry 1: src 0x99e8, dst 0x100002fc, len 0x1ac4, handler 0x2104 (zero-init)
 * When the table is exhausted it calls rt_entry (__rt_entry).
 *
 * Code that the disassembler shows as .word data in this range:
 *   0x2104-0x211f  __scatterload_zeroinit: zero-fill r1[0..r2)
 *   0x23d6-0x240f  __decompress: zero-run RLE decoder for .data
 */
void rt_scatterload(void);

/* 0x2120  callers: 1 */
/* Keil __rt_lib_init stub: push {r0-r4,lr}; pop {r0-r4,pc}. Does nothing. */
void rt_lib_init(void);

/* 0x2124  callers: 1 */
/* Keil __rt_lib_shutdown stub. Does nothing. */
void rt_lib_shutdown(void);

/* 0x2128  callers: 2 */
/* Keil __rt_entry: set up stack/heap, library init, main, exit. */
void rt_entry(void)
{
    rt_user_setup_stackheap();              /* __user_setup_stackheap */
    rt_lib_init();              /* library init stub (r1 = r2 passed in) */
    main();
    rt_exit_main();              /* exit(main's return value) -- never returns */
}

/* 0x213a  callers: 1 */
/* Keil __rt_exit: library shutdown then _sys_exit. Does not return. */
void rt_exit(int code)
{
    rt_lib_shutdown();
    rt_sys_exit(code);
}

/* 0x2148  callers: 0 (vector 1) */
/* Reset_Handler: branches to rt_main (__main) through a literal. */
void rt_reset_handler(void)
{
    rt_main();
}

/* 0x214c  callers: 0 (vector 2, NMI) */
/* Default NMI handler: infinite loop. */
void rt_nmi_handler(void) { for (;;) ; }

/* 0x214e  callers: 0 (vector 3, HardFault) */
/* Default HardFault handler: infinite loop. */
void rt_hardfault_handler(void) { for (;;) ; }

/* 0x2150  callers: 0 (vector 11, SVCall) */
/* Default SVC handler: infinite loop. */
void rt_svc_handler(void) { for (;;) ; }

/* 0x2152  callers: 0 (vector 14, PendSV) */
/* Default PendSV handler: infinite loop. (A second, unreferenced `b .` sits
 * at 0x2154 -- the unused default SysTick handler; the real one is 0x7bd4.) */
void rt_pendsv_handler(void) { for (;;) ; }

/* 0x2156  callers: 0 (vector 47 = IRQ31) */
/* Default handler for IRQ31: infinite loop. */
void rt_irq31_handler(void) { for (;;) ; }

/* 0x2158  callers: 0 (every otherwise unused IRQ vector) */
/* Default IRQ handler: infinite loop. */
void rt_default_irq_handler(void) { for (;;) ; }

/* 0x215c  callers: 1 */
/* Keil __user_initial_stackheap. Returns four values in r0-r3:
 *   r0 heap base  = 0x10001ac0
 *   r1 stack top  = 0x10001dc0
 *   r2 heap limit = 0x10001bc0
 *   r3 stack limit= 0x10001bc0 */
void rt_user_initial_stackheap(void);

/* 0x217c  callers: 1 */
/* Keil memcmp(a, b, n): word-at-a-time when both pointers are aligned,
 * returns <0 / 0 / >0.
 *
 * The same listing block also contains the shared memset bodies that the
 * three entry points below jump into:
 *   0x21d6  _memset_w(p, n, word): store words, then a trailing half/byte
 *   0x21f0  _memset(p, n, word):   align p to 4 bytes, then fall into 0x21d6
 */
int rt_memcmp(const void *a, const void *b, u32 n);

/* 0x220e  callers: 3 */
/* Keil __aeabi_memclr(p, n): r2 = 0, branch to the memset body at 0x21f0. */
void rt_memclr(void *p, u32 n);

/* 0x2212  callers: 2 */
/* Keil __aeabi_memclr4(p, n): p already word aligned; r2 = 0, branch to
 * the word loop at 0x21d6. */
void rt_memclr4(void *p, u32 n);

/* 0x2216  callers: 18 */
/* Keil __aeabi_uidivmod(num, den): unsigned divide, quotient in r0 and
 * remainder in r1. Shares its shift/subtract body with rt_idivmod below. */
u32 rt_uidiv(u32 num, u32 den);

/* 0x222a  callers: 19 */
/* Seeded name rt_idivmod. The entry tests the sign of (num | den) and has a
 * negate-and-fix-up path, i.e. it is really the signed entry
 * (__aeabi_idivmod); for the non-negative operands every caller passes it
 * behaves as an unsigned divide. Quotient in r0, remainder in r1.
 * Divide by zero ends at 0x235e, which returns 0 (the __aeabi_idiv0 call is
 * nopped out). */
s32 rt_idivmod(s32 num, s32 den);

/* 0x2376  callers: 1 */
/* Keil __user_setup_stackheap: gets the libspace pointer (rt_user_libspace), runs on
 * a temporary stack inside it, calls rt_user_initial_stackheap, zeroes 0x40 bytes of libspace,
 * and installs sp = stack top (0x10001dc0). */
void rt_user_setup_stackheap(void);

/* 0x23b4  callers: 1 */
/* Keil exit(code): calls rt_exit(code). Does not return. */
void rt_exit_main(int code)
{
    rt_exit(code);
}

/* 0x23c0  callers: 1 */
/* Keil __user_libspace: returns the 0x60-byte library work area. */
void *rt_user_libspace(void)
{
    return (void *)0x10001a5c;      /* g_1a5c */
}

/* 0x23c8  callers: 1 */
/* Keil _sys_exit: semihosting SYS_EXIT (r0 = 0x18, r1 = 0x20026
 * ADP_Stopped_ApplicationExit, bkpt 0xab), then spins forever. */
void rt_sys_exit(int code)
{
    /* bkpt 0xab */
    for (;;) ;
}

/* ===================================================================== */
/* Tone generator                                                        */
/* ===================================================================== */

/* 0x2410  callers: 6 */
/* Start a tone: program SCT0 for `freq` (pwm_init divides 48000000 by
 * 5*freq), 50 % duty on PWM channel 2, and latch the "tone on" flag unless
 * tone_inhibit_flag is set. */
void tone_start(u32 freq)
{
    pwm_init(freq);
    pwm_set(2, 50);
    if (tone_inhibit_flag != 0)
        tone_on = 0;
    else
        tone_on = 1;
}

/* 0x2444  callers: 5 */
/* Stop the tone: 0 % duty on PWM channel 2 and return the pin to its GPIO
 * function (IOCON function bits cleared). */
void tone_stop(void)
{
    pwm_set(2, 0);
    REG32(0x40044110) = REG32(0x40044110) & ~7u;    /* IOCON PIO2_7 */
    REG32(0x40044110) = REG32(0x40044110);          /* read back, write again */
}

/* 0x2468  callers: 1 (CT32B1 handler, 0x7e76) */
/* Tone sequencer tick. tone_pattern = running pattern (set by tone_play),
 * tone_step = step inside the pattern, tone_ticks = ticks spent in this step.
 * Every pattern ends with tone_stop(), tone_pattern = 0 and P1_20 = 1. */
void tone_tick(void)
{
    tone_ticks++;

    switch (tone_pattern) {
    case 0:                             /* idle */
        tone_on = 0;
        tone_step = 0;
        tone_ticks = 0;
        break;

    case 1:                             /* started by tone_play(0) with 329 */
        switch (tone_step) {
        case 0:
            if (tone_ticks > 4) {
                tone_ticks = 0;
                tone_start(392);           /* 0x188 */
                tone_step = 1;
            }
            break;
        case 1:
            if (tone_ticks > 4) {
                tone_stop();
                tone_pattern = 0;
                gpio_wr(P(1,20), 1);
            }
            break;
        }
        break;

    case 2:                             /* started by tone_play(1) with 261 */
        switch (tone_step) {
        case 0:
            if (tone_ticks > 2) {
                tone_stop();
                tone_ticks = 0;
                tone_step = 1;
                gpio_wr(P(1,20), 1);
            }
            break;
        case 1:
            if (tone_ticks > 2) {
                tone_ticks = 0;
                tone_start(261);           /* 0x105 */
                tone_step = 2;
            }
            break;
        case 2:
            if (tone_ticks > 2) {
                tone_stop();
                tone_pattern = 0;
                gpio_wr(P(1,20), 1);
            }
            break;
        }
        break;

    case 3:                             /* started by tone_play(2) with 261 */
        if (tone_ticks > 6) {
            tone_stop();
            tone_pattern = 0;
            gpio_wr(P(1,20), 1);
        }
        break;

    case 4:                             /* started by tone_play(3) with 329 */
        if (tone_ticks > 0x28) {
            tone_stop();
            tone_pattern = 0;
            gpio_wr(P(1,20), 1);
        }
        break;

    default:
        break;
    }
}

/* 0x25a0  callers: 1 (led_mode, 0x3d86) */
/* Start tone pattern `which` (0..3; 4 and above do nothing). Skipped when
 * power_state_code == 6 or when eeprom_read_checked_3e03() returns 0xAA (EEPROM cells
 * 0x3e03/0x3e04 hold AA/55, i.e. the value 0xAA with a valid complement).
 * 392/329/261 look like the notes G4/E4/C4.  P1_20 is driven low for the
 * duration of a pattern and high again by tone_tick when it ends. */
void tone_play(u8 which)
{
    u8 tmp;

    tone_inhibit_flag = 0;
    if (power_get_state_code() == 6)                 /* power_state_code */
        return;
    if (eeprom_read_checked_3e03() == 0xaa)
        return;
    setting_get_1a43(&tmp);                      /* tmp = g_1a43; value is not used */

    switch (which) {
    case 0:
        gpio_wr(P(1,8), 1);
        gpio_wr(P(1,20), 0);
        tone_pattern = 1;
        tone_start(329);                   /* 0x149 */
        break;
    case 1:
        gpio_wr(P(1,20), 0);
        tone_pattern = 2;
        tone_start(261);                   /* 0x105 */
        break;
    case 2:
        gpio_wr(P(1,20), 0);
        tone_pattern = 3;
        tone_start(261);
        break;
    case 3:
        gpio_wr(P(1,20), 0);
        tone_pattern = 4;
        tone_start(329);
        break;
    case 4:
    default:
        break;
    }
}

/* 0x2654  callers: 1 (power_off, 0x7414) */
/* Reset two state bytes: g_02a4 = 3, g_02c7 = 0. */
void fn_2654(void)
{
    g_02a4 = 3;
    g_02c7 = 0;
}

/* ===================================================================== */
/* USART0 frame link: device table and parser                            */
/* ===================================================================== */

/* 0x266c  callers: 3 */
/* Send the fixed USART0 frame AA 00 03 05 03 00 F5 (type 5, data 03 00). */
void uart0_send_frame_05_03(void)
{
    u8 f[7];

    f[0] = 0xaa;
    f[1] = 0;
    f[2] = 3;
    f[3] = 5;
    f[4] = 3;
    f[5] = 0;
    f[6] = 0xf5;
    uart0_send(f, 7);                      /* blocking USART0 transmit */
}

/* 0x2694  callers: 1 */
/* Compare two 6-byte addresses. Returns 0 if equal, 1 if different. */
u8 uart0_addr_differs(const u8 *a, const u8 *b)
{
    u8 i;

    for (i = 0; i < 6; i++) {
        if (a[i] != b[i])
            return 1;
    }
    return 0;
}

/* 0x26b8  callers: 3 */
/* Copy a 6-byte address to `out`: the own address uart0_own_addr when `own` is
 * non-zero, otherwise that of device record `idx`. Returns 6, or 0 when
 * idx is out of range. */
u8 uart0_dev_get_addr(u8 own, u8 idx, u8 *out)
{
    u8 i;

    if (own != 0) {
        for (i = 0; i < 6; i++)
            out[i] = uart0_own_addr[i];
        return 6;
    }
    if (idx < uart0_dev_count) {
        for (i = 0; i < 6; i++)
            out[i] = uart0_dev_table[idx].addr[i];
        return 6;
    }
    return 0;
}

/* 0x2710  callers: 1 */
/* Copy a name to `out`, at most 0x20 bytes: uart0_own_name (length uart0_own_name_len) when
 * `own` is non-zero, otherwise the name of device record `idx`.
 * Returns the stored (unclamped) length, or 0 when idx is out of range. */
u8 uart0_dev_get_name(u8 own, u8 idx, u8 *out)
{
    u8 i, n;

    if (own != 0) {
        if (uart0_own_name_len > 0x20)
            n = 0x20;
        else
            n = uart0_own_name_len;
        for (i = 0; i < n; i++)
            out[i] = uart0_own_name[i];
        return uart0_own_name_len;
    }
    if (idx < uart0_dev_count) {
        if (uart0_dev_table[idx].name_len > 0x20)
            n = 0x20;
        else
            n = uart0_dev_table[idx].name_len;
        for (i = 0; i < n; i++)
            out[i] = uart0_dev_table[idx].name[i];
        return uart0_dev_table[idx].name_len;
    }
    return 0;
}

/* 0x27a8  callers: 1 */
/* Number of device records in use. */
u8 get_uart0_dev_count(void)
{
    return uart0_dev_count;
}

/* 0x27b4  callers: 2 */
/* Flag set by a type-2 frame (see uart0_parse_frame). */
u8 get_uart0_link_flag(void)
{
    return uart0_link_flag;
}

/* 0x27c0  callers: 4 */
/* Clear all six device records and the record count. */
void uart0_dev_clear_all(void)
{
    u8 i;

    for (i = 0; i < 6; i++) {
        rt_memclr(uart0_dev_table[i].addr, 6);
        uart0_dev_table[i].kind = 0;
        rt_memclr(uart0_dev_table[i].name, 0x40);
        uart0_dev_table[i].name_len = 0;
    }
    uart0_dev_count = 0;
}

/* 0x2814  callers: 1 */
/* Parse one frame from the USART0 receive buffer.
 *   returns -2  frame not complete yet (keep the buffer)
 *           -3  type 3 with an address that is already in the table
 *           -1  everything else, including "not a frame"
 * The checksum byte is never verified. */
s32 uart0_parse_frame(u8 len, const u8 *buf)
{
    s32 plen = 0;
    u8 i;

    if (buf[0] != 0xaa)
        return -1;
    if (len < 6)
        return -2;
    plen = (buf[1] << 8) | buf[2];
    if (len < plen + 4)
        return -2;

    switch (buf[3]) {
    case 0:
    case 1:
        break;

    case 2:
        if (buf[4] == 0 || buf[4] == 2)
            uart0_link_flag = 1;
        else
            uart0_link_flag = 0;
        break;

    case 3:                             /* add a device: addr[6], kind */
        if (uart0_dev_count < 6) {
            for (i = 0; i < uart0_dev_count; i++) {
                if (uart0_addr_differs(uart0_dev_table[i].addr, &buf[4]) == 0)
                    return -3;
            }
            for (i = 0; i < 6; i++)
                uart0_dev_table[uart0_dev_count].addr[i] = buf[i + 4];
            uart0_dev_table[uart0_dev_count].kind = buf[10];
            uart0_dev_count++;
        }
        break;

    case 4:                             /* truncate the device list */
        if (buf[4] < uart0_dev_count)
            uart0_dev_count = buf[4];
        break;

    case 5:                             /* name, text starts at buf[10] */
        if (((plen - 7) < 0x40 && uart0_dev_sel < uart0_dev_count) || uart0_reply_mode != 0) {
            if (uart0_reply_mode != 0) {
                /* NB: this path has no length limit; uart0_own_name is 0x40 bytes */
                for (i = 0; (plen - 7) > i; i++)
                    uart0_own_name[i] = buf[i + 10];
                uart0_own_name_len = (u8)(plen - 7);
            } else {
                for (i = 0; (plen - 7) > i; i++)
                    uart0_dev_table[uart0_dev_sel].name[i] = buf[i + 10];
                uart0_dev_table[uart0_dev_sel].name_len = (u8)(plen - 7);
            }
        }
        break;

    case 6:
        break;

    case 0xff:                          /* own address, two bytes per reply */
        switch (uart0_reply_mode) {
        case 0:
            break;
        case 1:
            uart0_own_addr[0] = buf[4];
            uart0_own_addr[1] = buf[5];
            break;
        case 2:
            uart0_own_addr[2] = buf[4];
            uart0_own_addr[3] = buf[5];
            break;
        case 3:
            uart0_own_addr[4] = buf[4];
            uart0_own_addr[5] = buf[5];
            break;
        }
        break;

    default:
        break;
    }
    return -1;
}

/* 0x29f4  callers: 4 */
/* Clear the own-name buffer and its length. */
void uart0_own_name_clear(void)
{
    rt_memclr(uart0_own_name, 0x40);
    uart0_own_name_len = 0;
}

/* 0x2a10  callers: 3 */
/* Clear the type-2 flag. */
void uart0_link_flag_clear(void)
{
    uart0_link_flag = 0;
}

/* 0x2a1c  callers: 6 */
/* Select what the next type-5 / type-0xFF reply is stored as
 * (0 = name of the selected record, 1..3 = own address bytes 0-1/2-3/4-5,
 * non-zero also = own name). */
void uart0_set_reply_mode(u8 mode)
{
    uart0_reply_mode = mode;
}

/* 0x2a28  callers: 2 */
/* Select the device record that a type-5 name reply is stored into. */
void uart0_dev_select(u8 idx)
{
    if (idx < uart0_dev_count)
        uart0_dev_sel = idx;
}

/* 0x2a40  callers: 3 */
/* Detach USART0 from its pins: P0_18 / P0_19 to IOCON function 0,
 * both direction bits cleared (inputs), then iocon_cfg(.., 1) on each. */
void uart0_pins_release(void)
{
    gpio_as_input(P(0,18), 0);
    gpio_as_input(P(0,19), 0);
    gpio_dir_clear(0, 18);                     /* LPC_GPIO->DIR[0] &= ~(1 << 18) */
    gpio_dir_clear(0, 19);
    iocon_cfg(0, 18, 1);
    iocon_cfg(0, 19, 1);
}

/* 0x2a78  callers: 3 */
/* Attach USART0: P0_18 / P0_19 to IOCON function 1 (RXD / TXD) and
 * initialise the UART at 19200 baud. */
void uart0_pins_attach(void)
{
    gpio_as_input(P(0,18), 1);
    gpio_as_input(P(0,19), 1);
    uart0_init(0x4b00);
}

/* 0x2a94  callers: 1 (main loop, 0x8926) */
/* USART0 receive task. In power_state_code 3 everything received on USART0
 * is forwarded verbatim to USART2 (uart2_send is the blocking USART2
 * transmit); otherwise it is parsed as an 0xAA frame. The receive buffer is
 * emptied afterwards unless the frame is still incomplete. */
void uart0_task(void)
{
    u8 n = 0;

    if (power_get_state_code() == 3) {               /* power_state_code */
        n = (u8)get_uart0_rx_count();              /* USART0 rx byte count */
        if (n > 0) {
            uart2_send(uart0_rx_buf_ptr, n);
            uart0_rx_reset();                  /* empty the USART0 rx buffer */
        }
    } else {
        n = (u8)get_uart0_rx_count();
        if (n > 0) {
            if (uart0_parse_frame(n, uart0_rx_buf_ptr) != -2)
                uart0_rx_reset();
        }
    }
}

/* ===================================================================== */
/* I2C1 slave                                                            */
/* ===================================================================== */

/* 0x2ae0  callers: 17 */
/* Finish the reply already placed in the I2C1 buffer (i2c[0] = id,
 * i2c[1] = length, payload from i2c[2]): append the XOR checksum and hand
 * the total length to the I2C1 driver (i2c1_set_tx_len stores it in i2c1_tx_len with
 * IRQ10 masked). The XOR seed is (own slave address | 1), or 0x50 when
 * i2c1_reply_fixed_seed is non-zero. */
void i2c1_send_reply(void)
{
    u8 seed;
    u8 n;

    if (i2c1_reply_fixed_seed == 0)
        seed = i2c1_own_addr() | 1;           /* LPC_I2C1->ADR0, read bit set */
    else
        seed = 0x50;

    n = (i2c1_frame_ptr[1] & 0x7f) + 2;
    i2c1_frame_ptr[n] = cmd_xor_checksum(n, seed);       /* seed ^ i2c[0] ^ ... ^ i2c[n-1] */
    n = (u8)(n + 1);
    i2c1_set_tx_len(n);
}

/* 0x2b28  callers: 2 (USART1 task 0x611e, USART2 task 0x6cce) */
/* Wrapper that passes a received binary frame to uart_beef_frame_handler with three
 * scratch locals (initialised 0, 0x13, 7) and the value of g_02f1.
 * Always returns 0. */
u32 cmd_frame_dispatch(u32 a, u32 b)
{
    u32 v10 = 0;
    u32 v0c = 0x13;
    u32 v08 = 7;
    u32 r6 = 1;
    u32 ret = 0;

    r6 = power_get_flag_02f1();                     /* g_02f1 */
    uart_beef_frame_handler(a, b, &v10, &v0c, &v08, r6);
    return ret;
}

/* 0x2b60  callers: 1 (power_off, 0x742c) */
/* Fetch the I2C1 slave buffer address (0x100005ba) into i2c1_frame_ptr. */
void i2c1_buf_init(void)
{
    i2c1_frame_ptr = get_i2c1_buf();
}

/* 0x2b70  callers: 1 (main loop, 0x8922) */
/* I2C1 slave command task.
 *
 * Runs when the driver has received exactly (i2c[1] & 0x7f) + 3 bytes.
 * Masks IRQ10 (I2C1), checks the XOR of all bytes but the last (seeded with
 * the own slave address) against the last byte, resets the receive count,
 * and -- if the first byte was 0xF1 -- dispatches on the command byte i2c[2].
 * Arguments are i2c[3], i2c[4], ...
 *
 * NB: on a checksum mismatch the function returns with IRQ10 still masked.
 *
 * Command byte -> handler (decimal / hex):
 *    0 00 cmd_get_status        48 30 cmd_set_light_rails        71 47 cmd_get_02e6
 *    1 01 cmd_get_version        49 31 cmd_set_p2_17_n        72 48 cmd_set_p0_21_n
 *    3 03 (nothing)      50 32 cmd_nop        73 49 cmd_get_p2_5_and_p2_14
 *    4 04 inline         51 33 cmd_set_p1_29        74 4a cmd_get_uart4_flag
 *    7 07 (nothing)      52 34 cmd_set_sel_p1_12_13        75 4b cmd_get_uart4_data
 *    8 08 cmd_gpio_write        53 35 cmd_get_p0_20        76 4c cmd_set_p0_11
 *    9 09 cmd_gpio_read        54 36 cmd_set_p1_8_n        77 4d cmd_get_const_61
 *   22 16 cmd_get_status2        55 37 cmd_set_p0_22        80 50 inline
 *   24 18 cmd_power_off_fault        56 38 cmd_set_p1_21        81 51 inline
 *   26 1a cmd_led_mode        57 39 cmd_set_p0_16        82 52 inline
 *   32 20 cmd_get_fan_speeds        58 3a cmd_set_p0_17        83 53 inline
 *                        59 3b cmd_set_p1_26_n        84 54 cmd_uart0_send_record2
 *                        60 3c cmd_call_4528        85 55 cmd_uart0_send_record3
 *                        61 3d cmd_get_p1_30        86 56 inline
 *                        62 3e cmd_call_7998        87 57 inline
 *                        63 3f cmd_call_78c4        88 58 inline
 *                        64 40 cmd_set_p1_2        89 59 cmd_get_0146
 *                        65 41 cmd_set_p1_10        90 5a cmd_get_record
 *                        66 42 cmd_set_p1_17        91 5b cmd_get_0148
 *                        67 43 cmd_set_p2_13        92 5c cmd_uart0_send_select
 *                        68 44 cmd_set_p0_8
 *                        69 45 cmd_set_p1_19
 *                        70 46 cmd_get_02e5
 *   Unassigned (fall to default, no action): 2, 5, 6, 10-21, 23, 25, 27-31,
 *   33-47, 78, 79 and everything >= 93.
 *
 * Commands 80-88 emit USART0 0xAA frames (see file header); f[] below is the
 * 7-byte frame buffer at sp+8.
 */
void i2c1_cmd_task(void)
{
    u8  f[8];
    u8 *p;
    u32 first;
    u32 addr;
    s32 rx;
    u8  total;
    u8  n;
    u8  i;
    u8  sum;
    u8  ck;

    rx = get_i2c1_rx_count();                     /* i2c1_rx_count: bytes received */
    if (rx < 2)
        return;

    total = (u8)((i2c1_frame_ptr[1] & 0x7f) + 3);
    if (rx != total)
        return;

    REG32(0xe000e180) = 1 << 10;        /* NVIC ICER: mask I2C1 */

    addr  = i2c1_own_addr();                  /* LPC_I2C1->ADR0 */
    first = i2c1_frame_ptr[0];
    ck    = cmd_xor_checksum((u8)(total - 1), addr);
    i2c1_rx_count_clear();                          /* i2c1_rx_count = 0 */

    if (i2c1_frame_ptr[total - 1] != ck)
        return;                         /* IRQ10 is left masked */

    if (total == 3) {                   /* empty frame: answer id=addr, n=0x80 */
        i2c1_frame_ptr[0] = addr;
        i2c1_frame_ptr[1] = 0x80;
        i2c1_send_reply();
    }

    if (first == 0xf1) {
        i2c1_reply_fixed_seed = 0;

        switch (i2c1_frame_ptr[2]) {
        case 0:
            cmd_get_status();
            break;

        case 1:
            cmd_get_version();
            break;

        case 3:
            break;

        case 4:
            if (led_fault_latch == 0) {
                power_off_normal();              /* -> power_off(0) per trace */
                power_clear_on_flags();
            } else {
                gpio_sel_p1_13_12(0);
                gpio_wr(P(1,19), 0);
                gpio_wr(P(1,8), 0);
                gpio_sel_p2_21_20_n(0);
                power_clear_on_flags();
                delay_ticks(35000);        /* 0x88b8 */
                if (key_wait_power_loop() != 0) {   /* always returns 1 */
                    led_fault_latch = 0;
                    power_off_normal();
                    power_clear_on_flags();
                }
            }
            break;

        case 7:
            break;

        case 8:
            cmd_gpio_write();
            break;

        case 9:
            cmd_gpio_read();
            break;

        case 22:
            cmd_get_status2();
            break;

        case 24:
            cmd_power_off_fault();                  /* power_off(2) */
            break;

        case 26:
            cmd_led_mode();
            break;

        case 32:
            cmd_get_fan_speeds();
            break;

        case 48: cmd_set_light_rails(); break;
        case 49: cmd_set_p2_17_n(); break;
        case 50: cmd_nop(); break;
        case 51: cmd_set_p1_29(); break;
        case 52: cmd_set_sel_p1_12_13(); break;
        case 53: cmd_get_p0_20(); break;
        case 54: cmd_set_p1_8_n(); break;
        case 55: cmd_set_p0_22(); break;
        case 56: cmd_set_p1_21(); break;
        case 57: cmd_set_p0_16(); break;
        case 58: cmd_set_p0_17(); break;
        case 59: cmd_set_p1_26_n(); break;
        case 60: cmd_call_4528(); break;
        case 61: cmd_get_p1_30(); break;
        case 62: cmd_call_7998(); break;
        case 63: cmd_call_78c4(); break;
        case 64: cmd_set_p1_2(); break;
        case 65: cmd_set_p1_10(); break;
        case 66: cmd_set_p1_17(); break;
        case 67: cmd_set_p2_13(); break;
        case 68: cmd_set_p0_8(); break;
        case 69: cmd_set_p1_19(); break;
        case 70: cmd_get_02e5(); break;
        case 71: cmd_get_02e6(); break;
        case 72: cmd_set_p0_21_n(); break;
        case 73: cmd_get_p2_5_and_p2_14(); break;
        case 74: cmd_get_uart4_flag(); break;
        case 75: cmd_get_uart4_data(); break;
        case 76: cmd_set_p0_11(); break;
        case 77: cmd_get_const_61(); break;

        case 80:
            /* frames AA 00 03 05 09 00 EF, 10000 delay, AA 00 03 05 0A 00 EE */
            cmd_80_seen = 1;
            n = (u8)get_uart0_rx_count();          /* pending USART0 rx bytes ... */
            if (n > 0) {
                p = get_uart0_rx_buf();          /* USART0 rx buffer 0x100018c0 */
                uart2_send(p, n);          /* ... are pushed out on USART2 */
                uart0_rx_reset();
            }
            f[0] = 0xaa; f[1] = 0; f[2] = 3; f[3] = 5;
            f[4] = 9;    f[5] = 0; f[6] = 0xef;
            uart0_send(f, 7);
            delay_ticks(10000);            /* 0x2710 */
            n = (u8)get_uart0_rx_count();
            if (n > 0) {
                p = get_uart0_rx_buf();
                uart2_send(p, n);
                uart0_rx_reset();
            }
            f[0] = 0xaa; f[1] = 0; f[2] = 3; f[3] = 5;
            f[4] = 0xa;  f[5] = 0; f[6] = 0xee;
            uart0_send(f, 7);
            break;

        case 81:
            /* frames AA 00 03 05 0B 00 ED, 10000 delay, AA 00 03 05 0C 00 EC */
            cmd_81_seen = 1;
            n = (u8)get_uart0_rx_count();
            if (n > 0) {
                p = get_uart0_rx_buf();
                uart2_send(p, n);
                uart0_rx_reset();
            }
            f[0] = 0xaa; f[1] = 0; f[2] = 3; f[3] = 5;
            f[4] = 0xb;  f[5] = 0; f[6] = 0xed;
            uart0_send(f, 7);
            delay_ticks(10000);
            n = (u8)get_uart0_rx_count();
            if (n > 0) {
                p = get_uart0_rx_buf();
                uart2_send(p, n);
                uart0_rx_reset();
            }
            f[0] = 0xaa; f[1] = 0; f[2] = 3; f[3] = 5;
            f[4] = 0xc;  f[5] = 0; f[6] = 0xec;
            uart0_send(f, 7);
            break;

        case 82:
            /* clear the device table, send type 0 with two argument bytes */
            uart0_dev_clear_all();
            f[0] = 0xaa; f[1] = 0; f[2] = 3; f[3] = 0;
            f[4] = i2c1_frame_ptr[3];
            f[5] = i2c1_frame_ptr[4];
            sum = 0;
            for (i = 1; i < 6; i++)
                sum = (u8)(f[i] + sum);
            f[6] = (u8)(0x100 - sum);
            uart0_send(f, 7);
            break;

        case 83:
            /* frame AA 00 02 01 00 FD */
            f[0] = 0xaa; f[1] = 0; f[2] = 2; f[3] = 1; f[4] = 0;
            sum = 0;
            for (i = 1; i < 5; i++)
                sum = (u8)(f[i] + sum);
            f[5] = (u8)(0x100 - sum);
            uart0_send(f, 6);
            break;

        case 84:
            cmd_uart0_send_record2();
            break;

        case 85:
            cmd_uart0_send_record3();
            break;

        case 86:
            /* frame AA 00 02 04 0A F0 */
            f[0] = 0xaa; f[1] = 0; f[2] = 2; f[3] = 4; f[4] = 0xa;
            sum = 0;
            for (i = 1; i < 5; i++)
                sum = (u8)(f[i] + sum);
            f[5] = (u8)(0x100 - sum);
            uart0_send(f, 6);
            break;

        case 87:
            /* type 5 with two argument bytes; sub-codes 0x16 and 0x18 are
             * only sent while the type-2 flag uart0_link_flag is set */
            if (get_uart0_link_flag() == 0 &&
                (i2c1_frame_ptr[3] == 0x16 || i2c1_frame_ptr[3] == 0x18))
                break;
            f[0] = 0xaa; f[1] = 0; f[2] = 3; f[3] = 5;
            f[4] = i2c1_frame_ptr[3];
            f[5] = i2c1_frame_ptr[4];
            sum = 0;
            for (i = 1; i < 6; i++)
                sum = (u8)(f[i] + sum);
            f[6] = (u8)(0x100 - sum);
            uart0_send(f, 7);
            break;

        case 88:
            /* frame AA 00 02 06 00 F8 */
            f[0] = 0xaa; f[1] = 0; f[2] = 2; f[3] = 6; f[4] = 0;
            sum = 0;
            for (i = 1; i < 5; i++)
                sum = (u8)(f[i] + sum);
            f[5] = (u8)(0x100 - sum);
            uart0_send(f, 6);
            break;

        case 89:
            cmd_get_0146();
            break;

        case 90:
            cmd_get_record();
            break;

        case 91:
            cmd_get_0148();
            break;

        case 92:
            cmd_uart0_send_select();
            break;

        case 2:  case 5:  case 6:
        case 10: case 11: case 12: case 13: case 14: case 15:
        case 16: case 17: case 18: case 19: case 20: case 21:
        case 23: case 25:
        case 27: case 28: case 29: case 30: case 31:
        case 33: case 34: case 35: case 36: case 37: case 38: case 39:
        case 40: case 41: case 42: case 43: case 44: case 45: case 46:
        case 47:
        case 78: case 79:
        default:
            break;
        }
    }

    i2c1_stub_nop(0);                         /* empty function */
    REG32(0xe000e100) = 1 << 10;        /* NVIC ISER: unmask I2C1 */
}


/* ======================================================================
 * Part 2
 * ====================================================================== */

/*
 * part_2.c -- 0x30c0 .. 0x422f
 *
 * I2C1 command handlers (called from the 93-case switch at 0x2bfc in the
 * I2C1 command task 0x2b70), single-pin GPIO setters, the 0xBE 0xEF binary
 * frame handlers (0x3c20 for USART1/USART2, 0x3fc8 for USART4), led_mode
 * and two GPIO/IOCON primitives.
 *
 * Conventions used by every I2C1 handler in this file:
 *   i2c1_frame_ptr is a POINTER (u8 *) to the I2C1 frame buffer, shared for request
 *   and reply.
 *     request:  i2c1_frame_ptr[2] = command byte (switch index), i2c1_frame_ptr[3..] = args
 *     reply:    i2c1_frame_ptr[0] = i2c1_own_addr()          (LPC_I2C1->ADR0 & 0xff, own address)
 *               i2c1_frame_ptr[1] = 0x80 | payload_len
 *               i2c1_frame_ptr[2..] = payload
 *               i2c1_send_reply()                       (queue / send the reply)
 *   "case N" below = index into the switch at 0x2bfc that reaches the handler.
 *
 * Frames sent with uart0_send(buf, len) (USART0, start byte 0xAA):
 *     AA 00 <len?> <type> <payload...> <cksum>,
 *     cksum = (u8)(0x100 - sum(bytes[1 .. n-2]))
 */

extern u8 *i2c1_frame_ptr;            /* I2C1 frame buffer pointer */
extern u32 fan_capture0[], fan_capture1[], ct32b0_high_log[], fan_capture3[], fan_capture4[];
                              /* five capture blocks, 0x190 bytes apart, read as u32[] */
extern u16 fan_speed_last, g_0102, g_0104, g_0106, g_0108;   /* last reported value per channel */
extern u8  fan_speed_same_count, g_010b, g_010c, g_010d, g_010e;   /* "value unchanged" counters      */
extern u8  cmd_p1_8_asserted;
extern u8  uart_frame_class;
extern u8  uart4_flag, uart4_data_len;
extern u8  uart4_data[];
extern u8  led_phase, led_mode_current, led_fault_latch;

u32 rt_uidiv(u32 num, u32 den);            /* Keil __aeabi_uidiv: num / den */

/* 0x30c0  callers: 1 */
/* I2C1 case 24: power off with mode 2 (the "fault shutdown" mode). */
void cmd_power_off_fault(void)
{
    power_off(2);
}

/* 0x30cc  callers: 1 */
/* I2C1 case 9: read an arbitrary GPIO. Pin id is the single byte i2c1_frame_ptr[3],
 * so only port 0 pins are reachable. Reply: 1 byte, pin level. */
void cmd_gpio_read(void)
{
    u8 len = 1;

    i2c1_frame_ptr[0] = i2c1_own_addr();
    i2c1_frame_ptr[1] = 0x80 | len;
    i2c1_frame_ptr[2] = gpio_rd(i2c1_frame_ptr[3]);
    i2c1_send_reply();
}

/* 0x3100  callers: 1 */
/* I2C1 case 71: reply 1 byte = fn_48fc() (returns g_02e6). */
void cmd_get_02e6(void)
{
    u8 len = 1;

    i2c1_frame_ptr[0] = i2c1_own_addr();
    i2c1_frame_ptr[1] = 0x80 | len;
    i2c1_frame_ptr[2] = fn_48fc();
    i2c1_send_reply();
}

/* 0x312c  callers: 1 */
/* I2C1 case 70: reply 1 byte = fn_4908() (returns g_02e5). */
void cmd_get_02e5(void)
{
    u8 len = 1;

    i2c1_frame_ptr[0] = i2c1_own_addr();
    i2c1_frame_ptr[1] = 0x80 | len;
    i2c1_frame_ptr[2] = fn_4908();
    i2c1_send_reply();
}

/* 0x3158  callers: 1 */
/* I2C1 case 74: reply 1 byte = fn_4914() (returns uart4_flag, the flag set /
 * cleared by the USART4 frame handler uart4_beef_frame_handler). */
void cmd_get_uart4_flag(void)
{
    u8 len = 1;

    i2c1_frame_ptr[0] = i2c1_own_addr();
    i2c1_frame_ptr[1] = 0x80 | len;
    i2c1_frame_ptr[2] = fn_4914();
    i2c1_send_reply();
}

/* 0x3184  callers: 1 */
/* I2C1 case 75: reply with the uart4_data_len bytes stored in uart4_data[] by uart4_beef_frame_handler
 * (payload forwarded from a USART4 frame). */
void cmd_get_uart4_data(void)
{
    u8 len = fn_4920();               /* returns uart4_data_len */
    u8 i;

    i2c1_frame_ptr[0] = i2c1_own_addr();
    i2c1_frame_ptr[1] = 0x80 | len;
    for (i = 0; i < len; i++)
        i2c1_frame_ptr[i + 2] = uart4_data[i];
    i2c1_send_reply();
}

/* 0x31c8  callers: 1 */
/* I2C1 case 92: select 1..3 -> record it with uart0_set_reply_mode() (uart0_reply_mode = n) and send
 * the 7-byte frame AA 00 03 07 66 <04|06|08> ck via uart0_send. Any other value:
 * uart0_set_reply_mode(0) and nothing is sent. No I2C reply. */
void cmd_uart0_send_select(void)
{
    u8 frame[16];
    u8 sel = i2c1_frame_ptr[3];
    u8 sum, i;

    frame[0] = 0xaa;
    frame[1] = 0;
    frame[2] = 3;
    frame[3] = 7;

    if (sel == 1) {
        frame[4] = 0x66;
        frame[5] = 4;
        uart0_set_reply_mode(1);
    } else if (sel == 2) {
        frame[4] = 0x66;
        frame[5] = 6;
        uart0_set_reply_mode(2);
    } else if (sel == 3) {
        frame[4] = 0x66;
        frame[5] = 8;
        uart0_set_reply_mode(3);
    } else {
        uart0_set_reply_mode(0);
        return;
    }

    sum = 0;
    for (i = 1; i < 6; i++)
        sum = (u8)(sum + frame[i]);
    frame[6] = (u8)(0x100 - sum);
    uart0_send(frame, 7);
}

/* 0x3260  callers: 1 */
/* I2C1 case 91: reply 1 byte = get_uart0_link_flag() (returns uart0_link_flag). */
void cmd_get_0148(void)
{
    u8 len = 1;

    i2c1_frame_ptr[0] = i2c1_own_addr();
    i2c1_frame_ptr[1] = 0x80 | len;
    i2c1_frame_ptr[2] = get_uart0_link_flag();
    i2c1_send_reply();
}

/* 0x328c  callers: 1 */
/* I2C1 case 89: reply 1 byte = get_uart0_dev_count() (returns uart0_dev_count). */
void cmd_get_0146(void)
{
    u8 len = 1;

    i2c1_frame_ptr[0] = i2c1_own_addr();
    i2c1_frame_ptr[1] = 0x80 | len;
    i2c1_frame_ptr[2] = get_uart0_dev_count();
    i2c1_send_reply();
}

/* 0x32b8  callers: 1 */
/* I2C1 case 90: fetch a record. uart0_dev_get_name(b, a, dst) is tried first; if it
 * returns 0, status byte = 0 and uart0_dev_get_addr(b, a, dst) fills the data instead,
 * otherwise status byte = 1. Reply: status byte + n data bytes, where n is
 * the return value of whichever helper supplied the data. */
void cmd_get_record(void)
{
    u8 a = i2c1_frame_ptr[3];
    u8 b = i2c1_frame_ptr[4];
    u8 n;

    i2c1_frame_ptr[0] = i2c1_own_addr();
    n = uart0_dev_get_name(b, a, i2c1_frame_ptr + 3);
    if (n == 0) {
        i2c1_frame_ptr[2] = 0;
        n = uart0_dev_get_addr(b, a, i2c1_frame_ptr + 3);
    } else {
        i2c1_frame_ptr[2] = 1;
    }
    n = (u8)(n + 1);
    i2c1_frame_ptr[1] = 0x80 | n;
    i2c1_send_reply();
}

/* 0x331c  callers: 1 */
/* I2C1 case 61: reply 1 byte = level of P1_30 (the bit-banged MDIO data pin). */
void cmd_get_p1_30(void)
{
    u8 len = 1;

    i2c1_frame_ptr[0] = i2c1_own_addr();
    i2c1_frame_ptr[1] = 0x80 | len;
    i2c1_frame_ptr[2] = gpio_rd(P(1,30));
    i2c1_send_reply();
}

/* 0x334c  callers: 1 */
/* I2C1 case 53: reply 1 byte = level of P0_20. */
void cmd_get_p0_20(void)
{
    u8 len = 1;

    i2c1_frame_ptr[0] = i2c1_own_addr();
    i2c1_frame_ptr[1] = 0x80 | len;
    i2c1_frame_ptr[2] = gpio_rd(P(0,20));
    i2c1_send_reply();
}

/* 0x337c  callers: 1 */
/* I2C1 case 73: reply 1 byte = P2_5 AND P2_14. */
void cmd_get_p2_5_and_p2_14(void)
{
    u8 len = 1;
    u8 v;

    i2c1_frame_ptr[0] = i2c1_own_addr();
    i2c1_frame_ptr[1] = 0x80 | len;
    v  = gpio_rd(P(2,5));
    v &= gpio_rd(P(2,14));
    i2c1_frame_ptr[2] = v;
    i2c1_send_reply();
}

/* 0x33bc  callers: 1 */
/* I2C1 case 32: report five 16-bit readings, big-endian (10 payload bytes).
 * For each of five capture blocks: sum five u32 samples (words 10, 20, 30,
 * 40, 50 of the block, i.e. byte offsets 0x28/0x50/0x78/0xa0/0xc8), take
 * sum*4/5, and divide 2 880 000 000 (0xaba95000) by it -- a period-to-speed
 * conversion (fan tach RPM ?). A reading that has been identical for more
 * than 5 consecutive polls is reported as 0 (stalled / no new captures).
 * No divide-by-zero guard in the original. */
void cmd_get_fan_speeds(void)
{
    u32 k = 4;
    u32 t;                 /* spilled to [sp], otherwise unused */
    u16 v0, v1, v2, v3, v4;
    u32 len;

    t  = rt_uidiv(k * (fan_capture0[10] + fan_capture0[20] + fan_capture0[30] + fan_capture0[40] + fan_capture0[50]), 5);
    v0 = (u16)rt_uidiv(0xaba95000, t);
    t  = rt_uidiv(k * (fan_capture1[10] + fan_capture1[20] + fan_capture1[30] + fan_capture1[40] + fan_capture1[50]), 5);
    v1 = (u16)rt_uidiv(0xaba95000, t);
    t  = rt_uidiv(k * (ct32b0_high_log[10] + ct32b0_high_log[20] + ct32b0_high_log[30] + ct32b0_high_log[40] + ct32b0_high_log[50]), 5);
    v2 = (u16)rt_uidiv(0xaba95000, t);
    t  = rt_uidiv(k * (fan_capture3[10] + fan_capture3[20] + fan_capture3[30] + fan_capture3[40] + fan_capture3[50]), 5);
    v3 = (u16)rt_uidiv(0xaba95000, t);
    t  = rt_uidiv(k * (fan_capture4[10] + fan_capture4[20] + fan_capture4[30] + fan_capture4[40] + fan_capture4[50]), 5);
    v4 = (u16)rt_uidiv(0xaba95000, t);

    if (fan_speed_last == v0) {
        if (fan_speed_same_count < 0xff) fan_speed_same_count++;
        if (fan_speed_same_count > 5)    v0 = 0;
    } else {
        fan_speed_same_count = 0;
        fan_speed_last = v0;
    }
    if (g_0102 == v1) {
        if (g_010b < 0xff) g_010b++;
        if (g_010b > 5)    v1 = 0;
    } else {
        g_010b = 0;
        g_0102 = v1;
    }
    if (g_0104 == v2) {
        if (g_010c < 0xff) g_010c++;
        if (g_010c > 5)    v2 = 0;
    } else {
        g_010c = 0;
        g_0104 = v2;
    }
    if (g_0106 == v3) {
        if (g_010d < 0xff) g_010d++;
        if (g_010d > 5)    v3 = 0;
    } else {
        g_010d = 0;
        g_0106 = v3;
    }
    if (g_0108 == v4) {
        if (g_010e < 0xff) g_010e++;
        if (g_010e > 5)    v4 = 0;
    } else {
        g_010e = 0;
        g_0108 = v4;
    }

    len = 10;
    i2c1_frame_ptr[0]  = i2c1_own_addr();
    i2c1_frame_ptr[1]  = 0x80 | len;
    i2c1_frame_ptr[2]  = v0 >> 8;
    i2c1_frame_ptr[3]  = (u8)v0;
    i2c1_frame_ptr[4]  = v1 >> 8;
    i2c1_frame_ptr[5]  = (u8)v1;
    i2c1_frame_ptr[6]  = v2 >> 8;
    i2c1_frame_ptr[7]  = (u8)v2;
    i2c1_frame_ptr[8]  = v3 >> 8;
    i2c1_frame_ptr[9]  = (u8)v3;
    i2c1_frame_ptr[10] = v4 >> 8;
    i2c1_frame_ptr[11] = (u8)v4;
    i2c1_send_reply();
}

/* 0x368c  callers: 1 */
/* I2C1 case 1: firmware version. Reply 3 bytes: 'E' '0' '1' ("E01"). */
void cmd_get_version(void)
{
    u8 len = 3;

    i2c1_frame_ptr[0] = i2c1_own_addr();
    i2c1_frame_ptr[1] = 0x80 | len;
    i2c1_frame_ptr[2] = 0x45;
    i2c1_frame_ptr[3] = 0x30;
    i2c1_frame_ptr[4] = 0x31;
    i2c1_send_reply();
}

/* 0x36c8  callers: 1 */
/* I2C1 case 22: reply 1 byte built from an UNINITIALISED stack bit-field
 * struct with only bit 0 cleared -- i.e. bits 7..1 are stack garbage. Looks
 * like a status struct whose other fields were compiled out. */
void cmd_get_status2(void)
{
    u8 st;                 /* uninitialised in the original */
    u8 len;

    st &= ~1;
    len = 1;
    i2c1_frame_ptr[0] = i2c1_own_addr();
    i2c1_frame_ptr[1] = 0x80 | len;
    i2c1_frame_ptr[2] = st;
    i2c1_send_reply();
}

/* 0x3700  callers: 1 */
/* I2C1 case 0: status byte.
 *   bit 0     = power_get_last_off_mode() & 1     (returns power_last_off_mode)
 *   bits 3..1 = power_get_state_code() & 7     (returns power_state_code)
 *   bits 7..4 = power_get_wake_reason() & 0xf   (returns wake_reason)
 * The byte starts as uninitialised stack, but every bit is overwritten. */
void cmd_get_status(void)
{
    u8 st;                 /* uninitialised, fully overwritten below */
    u8 len;

    st = (st & ~0x01) | (power_get_last_off_mode() & 1);
    st = (st & ~0x0e) | ((power_get_state_code() << 1) & 0x0e);
    st = (st & ~0xf0) | ((power_get_wake_reason() << 4) & 0xf0);

    len = 1;
    i2c1_frame_ptr[0] = i2c1_own_addr();
    i2c1_frame_ptr[1] = 0x80 | len;
    i2c1_frame_ptr[2] = st;
    i2c1_send_reply();
}

/* 0x3770  callers: 5 */
/* Power off with mode 0 (normal). Used by I2C1 case 4, the IR/key paths at
 * 0x4cd0/0x4d12 and the main loop (0x8f30). */
void power_off_normal(void)
{
    power_off(0);
}

/* 0x377c  callers: 1 */
/* I2C1 case 77: reply 1 constant byte 0x61 ('a'). */
void cmd_get_const_61(void)
{
    u8 len = 1;

    i2c1_frame_ptr[0] = i2c1_own_addr();
    i2c1_frame_ptr[1] = 0x80 | len;
    i2c1_frame_ptr[2] = 0x61;
    i2c1_send_reply();
}

/* 0x37a8  callers: 1 */
/* I2C1 case 8: write an arbitrary GPIO: pin id = i2c1_frame_ptr[3] (one byte, so
 * port 0 only), value = i2c1_frame_ptr[4]. */
void cmd_gpio_write(void)
{
    gpio_wr(i2c1_frame_ptr[3], i2c1_frame_ptr[4]);
}

/* 0x37c0  callers: 1 */
/* I2C1 case 26: led_mode(arg). */
void cmd_led_mode(void)
{
    led_mode(i2c1_frame_ptr[3]);
}

/* ---- single-pin setters: arg = i2c1_frame_ptr[3]; "1" means on, anything else off.
 * Compiled as if/else with the same pin on both arms. ---------------------- */

/* 0x37d4  callers: 1 */
/* I2C1 case 67: P2_13 = (arg == 1). */
void cmd_set_p2_13(void)
{
    if (i2c1_frame_ptr[3] == 1)
        gpio_wr(P(2,13), 1);
    else
        gpio_wr(P(2,13), 0);
}

/* 0x37fc  callers: 1 */
/* I2C1 case 65: P1_10 = (arg == 1). */
void cmd_set_p1_10(void)
{
    if (i2c1_frame_ptr[3] == 1)
        gpio_wr(P(1,10), 1);
    else
        gpio_wr(P(1,10), 0);
}

/* 0x3824  callers: 1 */
/* I2C1 case 76: P0_11 = (arg == 1). */
void cmd_set_p0_11(void)
{
    if (i2c1_frame_ptr[3] == 1)
        gpio_wr(P(0,11), 1);
    else
        gpio_wr(P(0,11), 0);
}

/* 0x3848  callers: 1 */
/* I2C1 case 63: uart1_send_2a20_frame(arg0, arg1) (16-way switch on arg0, then a 200 ms
 * delay and a transmit -- lives in another part). */
void cmd_call_78c4(void)
{
    uart1_send_2a20_frame(i2c1_frame_ptr[3], i2c1_frame_ptr[4]);
}

/* 0x3860  callers: 1 */
/* I2C1 case 62: uart4_send_beef_frame(arg0, arg1) (18-way switch, builds a checksummed
 * frame -- lives in another part). */
void cmd_call_7998(void)
{
    uart4_send_beef_frame(i2c1_frame_ptr[3], i2c1_frame_ptr[4]);
}

/* 0x3878  callers: 1 */
/* I2C1 case 85: send the 11-byte frame AA 00 07 03 <6 bytes> ck via uart0_send,
 * the 6 bytes coming from uart0_dev_get_addr(0, arg, dst). No I2C reply. */
void cmd_uart0_send_record3(void)
{
    u8 frame[16];
    u8 idx = i2c1_frame_ptr[3];
    u8 sum, i;

    frame[0] = 0xaa;
    frame[1] = 0;
    frame[2] = 7;
    frame[3] = 3;
    uart0_dev_get_addr(0, idx, &frame[4]);

    sum = 0;
    for (i = 1; i < 10; i++)
        sum = (u8)(sum + frame[i]);
    frame[10] = (u8)(0x100 - sum);
    uart0_send(frame, 11);
}

/* 0x38cc  callers: 1 */
/* I2C1 case 84: like cmd_uart0_send_record3 but frame type 2 and with a source selector.
 *   b == 0: uart0_set_reply_mode(0) (uart0_reply_mode = 0); uart0_dev_select(a) (uart0_dev_sel = a if a < uart0_dev_count)
 *   b != 0: uart0_set_reply_mode(1); uart0_dev_select(0); uart0_own_name_clear() (clears the 0x40-byte buffer
 *           uart0_own_name and uart0_own_name_len)
 * then sends AA 00 07 02 <6 bytes from uart0_dev_get_addr(b, a, dst)> ck. */
void cmd_uart0_send_record2(void)
{
    u8 frame[20];
    u8 a = i2c1_frame_ptr[3];
    u8 b = i2c1_frame_ptr[4];
    u8 sum, i;

    frame[0] = 0xaa;
    frame[1] = 0;
    frame[2] = 7;
    frame[3] = 2;

    if (b == 0) {
        uart0_set_reply_mode(0);
        uart0_dev_select(a);
    } else {
        uart0_set_reply_mode(1);
        uart0_dev_select(0);
        uart0_own_name_clear();
    }
    uart0_dev_get_addr(b, a, &frame[4]);

    sum = 0;
    for (i = 1; i < 10; i++)
        sum = (u8)(sum + frame[i]);
    frame[10] = (u8)(0x100 - sum);
    uart0_send(frame, 11);
}

/* 0x394c  callers: 1 */
/* I2C1 case 48: bring the P2_23 / P1_15 / P1_18 trio up or down on demand
 * (same pins and 300 ms / 500 ms / 10 s timing as in power_on_rails; light
 * source / laser driver enable ?).
 *   on : USART0 pins to outputs + uart0_init(19200) (uart0_pins_attach), reset USART0
 *        RX state (uart0_rx_reset), P2_23=1, 300 ms, P1_15=1, 500 ms, P1_18=1, 10 s,
 *        P2_23=0, 10 s, then uart0_send_frame_05_03() (sends a 7-byte USART0 frame).
 *   off: USART0 pins P0_18/P0_19 back to pulled-down inputs (uart0_pins_release),
 *        P1_18=0, 300 ms, P1_15=0, then clear the associated state
 *        (uart0_link_flag_clear, uart0_dev_clear_all, uart0_own_name_clear).
 * Blocks for ~21 s in the "on" direction. */
void cmd_set_light_rails(void)
{
    if (i2c1_frame_ptr[3] == 1) {
        uart0_pins_attach();
        uart0_rx_reset();
        gpio_wr(P(2,23), 1);
        delay_ticks(300);
        gpio_wr(P(1,15), 1);
        delay_ticks(500);
        gpio_wr(P(1,18), 1);
        delay_ticks(10000);
        gpio_wr(P(2,23), 0);
        delay_ticks(10000);
        uart0_send_frame_05_03();
    } else {
        uart0_pins_release();
        gpio_wr(P(1,18), 0);
        delay_ticks(300);
        gpio_wr(P(1,15), 0);
        uart0_link_flag_clear();
        uart0_dev_clear_all();
        uart0_own_name_clear();
    }
}

/* 0x39e0  callers: 1 */
/* I2C1 case 51: P1_29 = (arg == 1). (P1_29 is also the bit-banged MDC.) */
void cmd_set_p1_29(void)
{
    if (i2c1_frame_ptr[3] == 1)
        gpio_wr(P(1,29), 1);
    else
        gpio_wr(P(1,29), 0);
}

/* 0x3a08  callers: 1 */
/* I2C1 case 58: P0_17 = (arg == 1). */
void cmd_set_p0_17(void)
{
    if (i2c1_frame_ptr[3] == 1)
        gpio_wr(P(0,17), 1);
    else
        gpio_wr(P(0,17), 0);
}

/* 0x3a2c  callers: 1 */
/* I2C1 case 59: P1_26 = !(arg == 1)  (active-low). */
void cmd_set_p1_26_n(void)
{
    if (i2c1_frame_ptr[3] == 1)
        gpio_wr(P(1,26), 0);
    else
        gpio_wr(P(1,26), 1);
}

/* 0x3a54  callers: 1 */
/* I2C1 case 60: pulse_start(arg0, arg1, arg2, arg3) (arms something through
 * pulse_state.. -- lives in another part). */
void cmd_call_4528(void)
{
    pulse_start(i2c1_frame_ptr[3], i2c1_frame_ptr[4], i2c1_frame_ptr[5], i2c1_frame_ptr[6]);
}

/* 0x3a78  callers: 1 */
/* I2C1 case 56: P1_21 = (arg == 1). */
void cmd_set_p1_21(void)
{
    if (i2c1_frame_ptr[3] == 1)
        gpio_wr(P(1,21), 1);
    else
        gpio_wr(P(1,21), 0);
}

/* 0x3aa0  callers: 1 */
/* I2C1 case 57: P0_16 = (arg == 1). */
void cmd_set_p0_16(void)
{
    if (i2c1_frame_ptr[3] == 1)
        gpio_wr(P(0,16), 1);
    else
        gpio_wr(P(0,16), 0);
}

/* 0x3ac4  callers: 1 */
/* I2C1 case 50: empty stub. */
void cmd_nop(void)
{
}

/* 0x3ac8  callers: 1 */
/* I2C1 case 55: P0_22 = (arg == 1). */
void cmd_set_p0_22(void)
{
    if (i2c1_frame_ptr[3] == 1)
        gpio_wr(P(0,22), 1);
    else
        gpio_wr(P(0,22), 0);
}

/* 0x3aec  callers: 1 */
/* I2C1 case 54: P1_8 = !(arg == 1) (active-low), and remember the request
 * in cmd_p1_8_asserted (1 = asserted). */
void cmd_set_p1_8_n(void)
{
    if (i2c1_frame_ptr[3] == 1) {
        cmd_p1_8_asserted = 1;
        gpio_wr(P(1,8), 0);
    } else {
        cmd_p1_8_asserted = 0;
        gpio_wr(P(1,8), 1);
    }
}

/* 0x3b24  callers: 1 */
/* I2C1 case 52: gpio_sel_p1_13_12(arg) -- the 2-bit selector on P1_12/P1_13. */
void cmd_set_sel_p1_12_13(void)
{
    gpio_sel_p1_13_12(i2c1_frame_ptr[3]);
}

/* 0x3b38  callers: 1 */
/* I2C1 case 69: P1_19 = (arg == 1). */
void cmd_set_p1_19(void)
{
    if (i2c1_frame_ptr[3] == 1)
        gpio_wr(P(1,19), 1);
    else
        gpio_wr(P(1,19), 0);
}

/* 0x3b60  callers: 1 */
/* I2C1 case 66: P1_17 = (arg == 1). */
void cmd_set_p1_17(void)
{
    if (i2c1_frame_ptr[3] == 1)
        gpio_wr(P(1,17), 1);
    else
        gpio_wr(P(1,17), 0);
}

/* 0x3b88  callers: 1 */
/* I2C1 case 72: P0_21 = !(arg == 1)  (active-low). */
void cmd_set_p0_21_n(void)
{
    if (i2c1_frame_ptr[3] == 1)
        gpio_wr(P(0,21), 0);
    else
        gpio_wr(P(0,21), 1);
}

/* 0x3bac  callers: 1 */
/* I2C1 case 64: P1_2 = (arg == 1). */
void cmd_set_p1_2(void)
{
    if (i2c1_frame_ptr[3] == 1)
        gpio_wr(P(1,2), 1);
    else
        gpio_wr(P(1,2), 0);
}

/* 0x3bd4  callers: 1 */
/* I2C1 case 68: P0_8 = (arg == 1). */
void cmd_set_p0_8(void)
{
    if (i2c1_frame_ptr[3] == 1)
        gpio_wr(P(0,8), 1);
    else
        gpio_wr(P(0,8), 0);
}

/* 0x3bf8  callers: 1 */
/* I2C1 case 49: P2_17 = !(arg == 1)  (active-low). */
void cmd_set_p2_17_n(void)
{
    if (i2c1_frame_ptr[3] == 1)
        gpio_wr(P(2,17), 0);
    else
        gpio_wr(P(2,17), 1);
}

/* 0x3c20  callers: 1 */
/* Binary frame handler for USART1 / USART2 (called from cmd_frame_dispatch; first
 * argument is unused). Frame layout as used here:
 *   [0]=0xBE [1]=0xEF [2]=0x10 [3]=payload length [5..6]=CRC16 little-endian
 *   [7],[8]=class: 0x11 0x11 or 0x22 0x22   [9]=sub-type   [11]=value
 * CRC is crc16_modbus(buf, n) computed with bytes 5,6 zeroed; it is accepted if
 * it matches over either buf[3]+7 bytes or over a fixed 12 bytes.
 *   class 0x11: uart_frame_class = 0; value -> cmd_binary_ack(value) once the CRC is good.
 *               value is taken only for (len 5, sub 1) or (len 6, sub 2),
 *               otherwise 0.
 *   class 0x22: uart_frame_class = 1; cmd_exec_code(value) is called BEFORE the CRC check
 *               and then again if the CRC is good.
 * cmd_binary_ack(1) and cmd_exec_code(0x4e) are the paths that raise wake reason 4
 * (clear uart_wake_token).
 * Notes: bytes 5,6 are left zeroed (not restored, unlike uart4_beef_frame_handler). If the
 * class is neither 0x11 nor 0x22, uart_frame_class keeps its value from the previous
 * frame and that handler is called with value 0 when the CRC matches.
 * Always returns 1. */
u32 uart_beef_frame_handler(u32 unused, u8 *buf)
{
    u32 value = 0;
    u16 rx_crc, crc_full, crc_12;

    if (buf[0] != 0xbe || buf[1] != 0xef || buf[2] != 0x10)
        return 1;

    if (buf[7] == 0x11 && buf[8] == 0x11) {
        uart_frame_class = 0;
        if (buf[3] == 5 && buf[9] == 1)
            value = buf[11];
        else if (buf[3] == 6 && buf[9] == 2)
            value = buf[11];
    } else if (buf[7] == 0x22 && buf[8] == 0x22) {
        uart_frame_class = 1;
        if (buf[3] == 5 && buf[9] == 1)
            value = buf[11];
        cmd_exec_code(value);
    }

    rx_crc = (u16)(buf[5] + (buf[6] << 8));
    buf[5] = 0;
    buf[6] = 0;
    crc_full = crc16_modbus(buf, (u8)(buf[3] + 7));
    crc_12   = crc16_modbus(buf, 12);

    if (rx_crc == crc_full || rx_crc == crc_12) {
        if (uart_frame_class == 0)
            cmd_binary_ack(value);
        else if (uart_frame_class == 1)
            cmd_exec_code(value);
    }
    return 1;
}

/* 0x3cf0  callers: 17 */
/* led_mode(mode): set the indicator LED pattern.
 *
 * led_set_pattern(a, b, c, d, e) reprograms the blinker (inside a rt_nop(0) /
 * rt_nop(1) bracket): led_all_off() first drives ALL six LED pins low
 * (P2_10, P2_9, P2_2, P2_8, P2_6, P2_18), then led_a=a, led_b=b (which LED
 * for each of the two blink phases; 5 appears to mean "none" -- see led_write),
 * led_time1=c, led_time2=d (duration of each phase, in blinker ticks), led_alternate=e.
 * led_phase = 1 is set by some modes (blinker enable / restart ?).
 *
 * led_fault_latch is a latched fault code: once a fault mode (9..17) has set it,
 * every later led_mode() call returns without doing anything.
 * led_mode_current is the current mode; a repeated request is ignored.
 * Any non-zero mode first forces the PIO2_2 pin mux back to GPIO.
 *
 * Switch helper rt_switch8 at 0x3d20; table: 24 entries (0..23) + default.
 * Cases 23 and default share code. */
void led_mode(u8 mode)
{
    if (mode != 0) {
        LPC_IOCON->PIO2_2 &= ~7u;                 /* 0x400440fc: FUNC = 0 (GPIO) */
        LPC_IOCON->PIO2_2 = LPC_IOCON->PIO2_2;    /* read back, write unchanged  */
    }

    if (led_fault_latch != 0)
        return;
    if (led_mode_current == mode)
        return;

    switch (mode) {
    case 0:
        led_phase = 1;
        led_set_pattern(0, 0, 0x32, 0x32, 0);
        break;

    case 1:
        if (led_fault_latch == 0) {                        /* always true here */
            led_set_pattern(5, 5, 0x14, 0x14, 0);
            gpio_wr(P(2,2), 1);
            gpio_wr(P(2,18), 1);
            if (setting_s4_is_zero() != 0)                   /* setting 4 == 0 */
                tone_play(0);
        }
        break;

    case 2:
        break;

    case 3:
        gpio_wr(P(2,6), 0);
        gpio_wr(P(2,18), 0);
        break;

    case 4:
        led_phase = 1;
        led_set_pattern(0, 5, 0x3c, 0x14, 0);
        break;

    case 5:
        led_phase = 1;
        led_set_pattern(0, 1, 0x14, 0x14, 1);
        break;

    case 6:
    case 7:
    case 8:
        break;

    case 9:                                       /* fault 1 */
        led_phase = 1;
        led_set_pattern(1, 5, 4, 4, 0);
        gpio_wr(P(2,9), 1);
        gpio_wr(P(2,10), 1);
        led_fault_latch = 1;
        break;

    case 10:                                      /* fault 2 */
        led_phase = 1;
        led_set_pattern(3, 5, 4, 4, 0);
        led_fault_latch = 2;
        break;

    case 11:                                      /* fault 3 */
        led_set_pattern(5, 5, 4, 4, 0);
        gpio_wr(P(2,10), 1);
        led_fault_latch = 3;
        break;

    case 12:                                      /* fault 4 */
        led_set_pattern(5, 5, 4, 4, 0);
        gpio_wr(P(2,2), 1);
        gpio_wr(P(2,10), 1);
        led_fault_latch = 4;
        break;

    case 13:                                      /* fault 5 */
        led_phase = 1;
        led_set_pattern(5, 5, 4, 4, 0);
        gpio_wr(P(2,8), 1);
        gpio_wr(P(2,10), 1);
        led_fault_latch = 5;
        break;

    case 14:                                      /* fault 6 */
        led_set_pattern(1, 2, 4, 4, 0);
        led_fault_latch = 6;
        break;

    case 15:                                      /* fault 7 */
        led_set_pattern(2, 3, 4, 4, 0);
        gpio_wr(P(2,2), 1);
        led_fault_latch = 7;
        break;

    case 16:                                      /* fault 8 */
        led_set_pattern(5, 5, 4, 4, 0);
        gpio_wr(P(2,9), 1);
        led_fault_latch = 8;
        break;

    case 17:                                      /* fault 9 */
        led_phase = 1;
        led_set_pattern(2, 5, 4, 4, 0);
        led_fault_latch = 9;
        break;

    case 18:
        led_set_pattern(0, 1, 4, 4, 1);
        break;

    case 19:
        gpio_wr(P(2,6), 1);
        break;

    case 20:
        gpio_wr(P(2,6), 0);
        break;

    case 21:                                      /* LEDs off, wait 30 s, power on */
        led_set_pattern(5, 5, 0x14, 0x14, 0);
        power_clear_on_flags();                                /* power_is_on = 0, clears factory_test3_active/factory_test10_active */
        delay_ticks(30000);
        power_on();
        break;

    case 22:
        led_phase = 1;
        timer_ct32b1_count_reset();                                /* ct32b1_tick_count = 0 */
        led_set_pattern(0, 5, 10, 10, 0);
        break;

    case 23:
    default:
        led_set_pattern(5, 5, 0x14, 0x14, 0);
        break;
    }

    led_mode_current = mode;
}

/* 0x3fc8  callers: 1 */
/* Binary frame handler for USART4 (called from the USART4 task at 0x614a).
 * Same BE EF 10 framing as uart_beef_frame_handler; only class 0x22 0x22 is looked at.
 *   returns -1  bad header, or buf[3] != buf[9] + 4
 *   returns -2  frame longer than the bytes available (buf[3] + 7 > avail)
 *   returns  1  otherwise
 * CRC as in uart_beef_frame_handler but the alternative length is 13 and bytes 5,6 are put
 * back afterwards.
 *   CRC good, sub 1, value 7:              uart4_flag = 1
 *   CRC good, sub 2, value 7, arg3 == 0:   uart4_flag = 0
 *   CRC BAD,  value 0x11:  uart4_data_len = buf[9] - 1 and that many bytes from
 *                          buf[12..] are copied to uart4_data[] (read back over
 *                          I2C1 by cmd_get_uart4_data).  The copy really is on the
 *                          CRC-mismatch branch (0x404e -> 0x407e). */
s32 uart4_beef_frame_handler(u32 avail, u8 *buf, u32 arg3)
{
    u32 value = 0;
    u16 rx_crc, crc_full, crc_13;
    u8  i;

    if (buf[0] != 0xbe || buf[1] != 0xef || buf[2] != 0x10)
        return -1;
    if (buf[7] != 0x22 || buf[8] != 0x22)
        return 1;

    if ((s32)(buf[3] + 7) > (s32)avail)
        return -2;

    if (buf[3] == buf[9] + 4)
        value = buf[11];
    else
        return -1;

    rx_crc = (u16)(buf[5] + (buf[6] << 8));
    buf[5] = 0;
    buf[6] = 0;
    crc_full = crc16_modbus(buf, (u8)(buf[3] + 7));
    crc_13   = crc16_modbus(buf, 13);
    buf[5] = (u8)rx_crc;
    buf[6] = rx_crc >> 8;

    if (rx_crc == crc_full || rx_crc == crc_13) {
        if (buf[9] == 1 && value == 7) {
            uart4_flag = 1;
        } else if (buf[9] == 2 && value == 7 && arg3 == 0) {
            uart4_flag = 0;
        }
    } else {
        if (value == 0x11) {
            uart4_data_len = buf[9] - 1;
            for (i = 0; i < uart4_data_len; i++)
                uart4_data[i] = buf[i + 12];
        }
    }
    return 1;
}

/* 0x40c0  callers: 7 */
/* Make a pin an input: clear its bit in the GPIO direction register
 * (0xA0002000 + port*4). */
void gpio_dir_clear(u32 port, u32 pin)
{
    LPC_GPIO->DIR[port] &= ~(1u << pin);
}

/* 0x40e8  callers: 16 */
/* iocon_cfg(port, pin, mode): set the pull resistor (IOCON MODE field, bits
 * 4:3) of a pin. It does NOT touch the FUNC field, despite the "func" name
 * used in docs/nxp-mcu.md.
 *   mode 2 -> MODE = 10b (pull-up), mode 1 -> MODE = 01b (pull-down),
 *   anything else -> MODE = 00b (no pull).
 * Register address: port 0: 0x40044000 + 4*pin
 *                   port 1: 0x40044060 + 4*pin
 *                   port 2: pins 0,1: 0x400440f0 + 4*pin
 *                           pins 2+ : 0x400440fc + 4*(pin-2)
 *                   other ports: nothing. */
void iocon_cfg(u32 port, u32 pin, u32 mode)
{
    if (port == 0) {
        REG32(0x40044000 + pin * 4) &= ~0x18u;
        if (mode == 2)
            REG32(0x40044000 + pin * 4) |= 0x10;
        else if (mode == 1)
            REG32(0x40044000 + pin * 4) |= 0x08;
    } else if (port == 1) {
        REG32(0x40044060 + pin * 4) &= ~0x18u;
        if (mode == 2)
            REG32(0x40044060 + pin * 4) |= 0x10;
        else if (mode == 1)
            REG32(0x40044060 + pin * 4) |= 0x08;
    } else if (port == 2) {
        if (pin == 0 || pin == 1) {
            REG32(0x400440f0 + pin * 4) &= ~0x18u;
            if (mode == 2)
                REG32(0x400440f0 + pin * 4) |= 0x10;
            else if (mode == 1)
                REG32(0x400440f0 + pin * 4) |= 0x08;
        } else {
            REG32(0x400440fc + (pin - 2) * 4) &= ~0x18u;
            if (mode == 2)
                REG32(0x400440fc + (pin - 2) * 4) |= 0x10;
            else if (mode == 1)
                REG32(0x400440fc + (pin - 2) * 4) |= 0x08;
        }
    }
}


/* ======================================================================
 * Part 3
 * ====================================================================== */

/*
 * part_3.c  --  LPC11U67 application, 0x4230 .. 0x50d3
 *
 * GPIO primitives, blocking UART senders, settings getters / load_settings,
 * GINT/PINT helpers, pin-table init, I2C0 and I2C1 interrupt state machines
 * (both are the same combined master+slave NXP-style handler), the I2C0
 * slave command task and the I2C1 master "write" transaction.
 *
 * Register shorthand used below (LPC11U6x):
 *   LPC_GPIO   = 0xA0000000 : DIR[port] +0x2000, PIN[port] +0x2100,
 *                             SET[port] +0x2200, CLR[port] +0x2280
 *   LPC_PINT   = 0xA0004000 : ISEL 0x00, IENR 0x04, SIENR 0x08, CIENR 0x0C,
 *                             IENF 0x10, SIENF 0x14, CIENF 0x18, RISE 0x1C,
 *                             FALL 0x20, IST 0x24
 *   LPC_I2C0   = 0x40000000, LPC_I2C1 = 0x40020000 :
 *                             CONSET 0x00, STAT 0x04, DAT 0x08, ADR0 0x0C,
 *                             CONCLR 0x18
 *   I2C CON bits: AA = 0x04, SI = 0x08, STO = 0x10, STA = 0x20
 *   LPC_USART0 = 0x40008000 (16550 style: THR 0x00, LSR 0x14, THRE = 0x20)
 *   LPC_USART1/2/4 = 0x4006C000 / 0x40070000 / 0x4004C000
 *                    (new style: STAT 0x08, TXRDY = 0x04, TXDAT 0x1C)
 *
 * RAM used by this range:
 *   i2c0_buf[]  I2C0 data buffer (0x24 bytes used; 0x100005ba follows directly)
 *   i2c1_buf[]  I2C1 data buffer
 *   i2c0_master_state u32 I2C0 master state   i2c1_master_state u32 I2C1 master state
 *   i2c0_slave_state u32 I2C0 slave  state   i2c1_slave_state u32 I2C1 slave  state
 *   i2c0_idx u8  I2C0 buffer index   i2c1_idx u8  I2C1 buffer index
 *   i2c0_rx_count u8  I2C0 rx count       i2c1_rx_count u8  I2C1 rx count
 *   i2c0_tx_len u8  I2C0 tx length      i2c1_tx_len u8  I2C1 tx length
 *   i2c0_irq_seen u8  I2C0 "irq seen"     i2c1_irq_seen u8  I2C1 "irq seen"
 *   i2c0_nack u8  I2C0 master NACK    i2c1_error u8  I2C1 master NACK
 *   i2c0_buf_ptr u8* pointer to i2c0_buf (set by i2c0_buf_ptr_init)
 *   master state values: 0 idle/arb-lost, 1 START sent, 2 rep-START sent,
 *                        4 transfer in progress, 5 finished/aborted
 *   slave state values:  0 idle (STOP seen), 6 receiving, 7 transmitting,
 *                        5 master NACKed our last byte
 */

/* 0x4230  callers: 2
 * Raw GPIO write: force the pin to output, then drive it high or low. */
void gpio_port_write(u32 port, u32 pin, u32 level)
{
    LPC_GPIO->DIR[port] |= 1u << pin;
    if (level != 0)
        LPC_GPIO->SET[port] = 1u << pin;
    else
        LPC_GPIO->CLR[port] = 1u << pin;
}

/* 0x4280  callers: 1
 * Raw GPIO read: returns bit `pin` of PIN[port] as 0/1. */
u32 gpio_port_read(u32 port, u32 pin)
{
    if ((LPC_GPIO->PIN[port] >> pin) & 1)
        return 1;
    return 0;
}

/* 0x42a8  callers: 1
 * Enter sleep: program PDSLEEPCFG, optionally set SLEEPDEEP, then WFI. */
void power_sleep_wfi(u32 deep, u32 pdsleepcfg)
{
    LPC_SYSCON->PDSLEEPCFG = pdsleepcfg;        /* 0x40048230 */
    if (deep != 0)
        SCB->SCR |= 4;                          /* 0xE000ED10, SLEEPDEEP */
    __wfi();
}

/* 0x42c8  callers: 1
 * USART0 blocking transmit of `len` bytes.  Note the do-while shape:
 * len == 0 would send 2^32 bytes. */
void uart0_send_blocking(const u8 *buf, u32 len)
{
    u32 more = 1;
    while (more == 1) {
        while ((LPC_USART0->LSR & 0x20) == 0) ;     /* wait THRE */
        LPC_USART0->THR = *buf;
        while ((LPC_USART0->LSR & 0x20) == 0) ;     /* wait THRE again */
        buf++;
        len--;
        if (len == 0)
            more = 0;
    }
}

/* 0x4304  callers: 1
 * USART1 blocking transmit of `len` bytes. */
void uart1_send_blocking(const u8 *buf, u32 len)
{
    u32 i;
    for (i = 0; i < len; i++) {
        while ((LPC_USART1->STAT & 4) == 0) ;       /* wait TXRDY */
        LPC_USART1->TXDAT = *buf;
        while ((LPC_USART1->STAT & 4) == 0) ;
        buf++;
    }
}

/* 0x433c  callers: 1
 * USART2 blocking transmit of `len` bytes. */
void uart2_send_blocking(const u8 *buf, u32 len)
{
    u32 i;
    for (i = 0; i < len; i++) {
        while ((LPC_USART2->STAT & 4) == 0) ;
        LPC_USART2->TXDAT = *buf;
        while ((LPC_USART2->STAT & 4) == 0) ;
        buf++;
    }
}

/* 0x4374  callers: 1
 * USART4 blocking transmit of `len` bytes. */
void uart4_send_blocking(const u8 *buf, u32 len)
{
    u32 i;
    for (i = 0; i < len; i++) {
        while ((LPC_USART4->STAT & 4) == 0) ;
        LPC_USART4->TXDAT = *buf;
        while ((LPC_USART4->STAT & 4) == 0) ;
        buf++;
    }
}

/* 0x43ac  callers: 1
 * Settings getter: field 2 (0x10001a4b). */
void setting_get_s2(u8 *out)
{
    *out = settings.s2;
}

/* 0x43b8  callers: 1 (phy_arm_wol)
 * Settings getter: copy the 6-byte MAC address. */
void setting_get_mac(u8 *out)
{
    u8 i;
    for (i = 0; i < 6; i++)
        out[i] = settings.mac[i];
}

/* 0x43d4  callers: 2
 * Classify the 4 bytes at 0x10001a0c (not written by load_settings; filled
 * elsewhere): {1,5,5,0} -> 0x55, {1,6,5,0} -> 0xAA, anything else returns
 * the argument unchanged (r0 is simply not touched). */
u32 setting_classify_1a0c(u32 dflt)
{
    if (g_1a0c[0] == 1 && g_1a0c[1] == 5 && g_1a0c[2] == 5 && g_1a0c[3] == 0)
        return 0x55;
    if (g_1a0c[0] == 1 && g_1a0c[1] == 6 && g_1a0c[2] == 5 && g_1a0c[3] == 0)
        return 0xaa;
    return dflt;
}

/* 0x4420  callers: 1 (setting_keep_rails)
 * Settings getter: standby mode (0x10001a4c). */
void setting_get_standby_mode(u8 *out)
{
    *out = settings.standby_mode;
}

/* 0x442c  callers: 1
 * Settings getter: field 4 (0x10001a4d). */
void setting_get_s4(u8 *out)
{
    *out = settings.s4;
}

/* 0x4438  callers: 1
 * Getter for the byte just below the settings block (0x10001a43). */
void setting_get_1a43(u8 *out)
{
    *out = g_1a43;
}

/* 0x4444  callers: 2 (USART2 "#00501" query, setting_wol_enabled)
 * Settings getter: WoL enable (0x10001a44). */
void setting_get_wol_enable(u8 *out)
{
    *out = settings.wol_enable;
}

/* 0x4450  callers: 1
 * Getter for the u32 at 0x10001a18 (same block as g_1a0c, offset 0xc). */
void setting_get_1a18(u32 *out)
{
    *out = g_1a18;
}

/* 0x445c  callers: 2
 * Load the settings block from the external EEPROM.  On the very first call
 * (eeprom_probed == 0) it also probes the device: if the first read at the default
 * address (0xAC) fails, switch to 0xA0 and re-read the pointer table. */
void load_settings(void)
{
    u32 ok = 0;

    ok = eeprom_read(0x3ff6, (u8 *)settings_ptrs, 10);     /* 5 x u16 pointer table */
    if (eeprom_probed == 0) {
        if (ok == 1) {
            eeprom_ac_present = 1;
            eeprom_probed = 1;
        } else {
            eeprom_ac_present = 0;
            eeprom_probed = 1;
        }
        if (eeprom_ac_present == 0) {
            eeprom_i2c_addr = 0xa0;
            eeprom_read(0x3ff6, (u8 *)settings_ptrs, 10);
        }
    }
    eeprom_read(settings_ptrs[0], &settings.wol_enable,   1);
    eeprom_read(settings_ptrs[1], &settings.mac[0],       6);
    eeprom_read(settings_ptrs[2], &settings.s2,           1);
    eeprom_read(settings_ptrs[3], &settings.standby_mode, 1);
    eeprom_read(settings_ptrs[4], &settings.s4,           1);
}

/* 0x4508  callers: 2 (USART2 "#00500 0/1")
 * Set WoL enable in RAM and store it to the EEPROM cell settings_ptrs[0]. */
void setting_set_wol_enable(u8 *val)
{
    settings.wol_enable = *val;
    eeprom_write(settings_ptrs[0], val, 1);
}

/* 0x4528  callers: 3 (I2C1 command handler 0x3a6e, main loop IR codes
 *                     0xF0 -> (0,20,20,3) and 0xE2 -> (1,20,20,3))
 * Arm the pulse generator that the 1 ms tick routine gpio_pulse_tick runs on the
 * pin pair P(1,3)/P(1,4): dir selects which pin is driven high, on/off are
 * phase lengths in units of 10 ticks, count is the number of pulses. */
void pulse_start(u8 dir, u32 on_time, u32 off_time, u8 count)
{
    pulse_state = 1;                         /* u8  phase / active */
    pulse_ticks = 0;                         /* u16 tick counter */
    pulse_dir = dir;                       /* u8 */
    pulse_on_ticks = (u16)(on_time * 10);       /* u16 */
    pulse_off_ticks = (u16)(off_time * 10);      /* u16 */
    pulse_done = 0;                         /* u8  pulses done */
    pulse_count = count;                     /* u8  pulses wanted */
}

/* 0x4574  callers: 0 (vector: GINT0 IRQ)
 * Group interrupt 0: just acknowledge (write-1-to-clear CTRL.INT). */
void gint0_irq(void)
{
    if (LPC_GINT0->CTRL & 1) {
        (void)LPC_GINT0->CTRL;          /* dummy read */
        LPC_GINT0->CTRL |= 1;
    }
}

/* 0x4598  callers: 0 (vector: GINT1 IRQ)
 * Group interrupt 1: same as GINT0. */
void gint1_irq(void)
{
    if (LPC_GINT1->CTRL & 1) {
        (void)LPC_GINT1->CTRL;
        LPC_GINT1->CTRL |= 1;
    }
}

/* 0x45bc  callers: 1
 * Acknowledge pin interrupt channel `ch` (edge-sensitive channels only). */
void pint_ack(u32 ch)
{
    if ((LPC_PINT->ISEL & (1u << ch)) == 0) {
        if (LPC_PINT->RISE & (1u << ch))
            LPC_PINT->RISE = 1u << ch;
        else if (LPC_PINT->FALL & (1u << ch))
            LPC_PINT->FALL = 1u << ch;
        LPC_PINT->IST = 1u << ch;
    }
}

/* 0x4608  callers: 1
 * Disable pin interrupt channel `ch`.  Edge channel: edge == 0 disables
 * falling, otherwise rising.  Level channel: disables the level interrupt.
 * (Faithful to the code: it ORs into the write-only CIENF/CIENR.) */
void pint_disable(u32 ch, u32 edge)
{
    if ((LPC_PINT->ISEL & (1u << ch)) == 0) {
        if (edge == 0)
            LPC_PINT->CIENF |= 1u << ch;
        else
            LPC_PINT->CIENR |= 1u << ch;
    } else {
        LPC_PINT->CIENR |= 1u << ch;
    }
}

/* 0x4650  callers: 1
 * Enable pin interrupt channel `ch`; same argument convention as pint_disable. */
void pint_enable(u32 ch, u32 edge)
{
    if ((LPC_PINT->ISEL & (1u << ch)) == 0) {
        if (edge == 0)
            LPC_PINT->SIENF |= 1u << ch;
        else
            LPC_PINT->SIENR |= 1u << ch;
    } else {
        LPC_PINT->SIENR |= 1u << ch;
    }
}

/* 0x4698  callers: 1
 * Is pin interrupt channel `ch` pending? */
u32 pint_pending(u32 ch)
{
    if (LPC_PINT->IST & (1u << ch))
        return 1;
    return 0;
}

/* 0x46b4  callers: 9
 * Rewrite a pin's IOCON word: clear FUNC (bits 2:0, i.e. select function 0)
 * and OR in `bits`.  IOCON register index by port:
 *   port 0: 0x40044000 + 4*pin
 *   port 1: 0x40044060 + 4*pin
 *   port 2: pins 0,1 -> 0x400440F0 + 4*pin ; pins >= 2 -> 0x400440FC + 4*(pin-2)
 * Each case is a clear-then-OR done as two separate read/modify/writes. */
void gpio_as_input(u32 id, u32 bits)
{
    u8 port = (u8)rt_idivmod((s32)id >> 8, 3).rem;     /* (id >> 8) % 3 */
    u8 pin  = (u8)id;
    volatile u32 *iocon;

    switch (port) {
    case 0:
        iocon = &REG32(0x40044000 + 4 * pin);
        break;
    case 1:
        iocon = &REG32(0x40044060 + 4 * pin);
        break;
    case 2:
        if (pin == 0 || pin == 1)
            iocon = &REG32(0x400440f0 + 4 * pin);
        else
            iocon = &REG32(0x400440fc + 4 * (pin - 2));
        break;
    default:
        return;
    }
    *iocon = (*iocon >> 3) << 3;
    *iocon = *iocon | bits;
}

/* 0x4780  callers: 23
 * Read a pin by id.  Returns 0/1. */
u32 gpio_rd(u32 id)
{
    u8 port = (u8)rt_idivmod((s32)id >> 8, 3).rem;
    u8 pin  = (u8)id;
    u32 v = 0;

    v = gpio_port_read(port, pin);
    return v;
}

/* 0x47a0  callers: 1 (power_off, 0x7408)
 * Apply the pin setup table rom_96b4 (80 entries x 8 bytes):
 *   +0 u16 id        (port << 8) | pin
 *   +2 u8  iocon     bits ORed into IOCON after FUNC is cleared
 *   +3 u8  dir       1 = output, 0 = input, anything else = leave alone
 *                    (entry 0, P0_0, has 0x40 here)
 *   +4 u8  cfg       output: bit 0 = initial level
 *                    input:  bits 3:2 = third argument of iocon_cfg
 *   +5 3 bytes padding
 * Skipped entirely when full == 0.  Always ends by driving P2_17 low. */
void gpio_init_table(u32 full)
{
    u8 i;

    if (full != 0) {
        LPC_SYSCON->SYSAHBCLKCTRL |= 0x40;          /* GPIO clock on */
        LPC_SYSCON->SYSAHBCLKCTRL &= ~0x800;        /* bit 11 off (SSP0 ?);
                                                       mask is 0x40048080 >> 19 */
        for (i = 0; i < 0x50; i++)
            gpio_as_input(rom_96b4[i].id, rom_96b4[i].iocon);

        for (i = 0; i < 0x50; i++) {
            u8 port = (u8)rt_idivmod((s32)rom_96b4[i].id >> 8, 3).rem;
            u8 pin  = (u8)rom_96b4[i].id;           /* low byte of id */

            if (rom_96b4[i].dir == 1) {
                gpio_port_write(port, pin, rom_96b4[i].cfg & 1);
            } else if (rom_96b4[i].dir == 0) {
                gpio_dir_clear(port, pin);                 /* clear DIR bit */
                iocon_cfg(port, pin, (rom_96b4[i].cfg >> 2) & 3);
            }
        }
    }
    gpio_wr(P(2,17), 0);
}

/* 0x485c  callers: 7
 * Set pin direction by id: out == 1 -> output, out == 0 -> input,
 * any other value -> nothing. */
void gpio_dir(u32 id, u32 out)
{
    u8 port = (u8)rt_idivmod((s32)id >> 8, 3).rem;
    u8 pin  = (u8)id;

    if (out == 1)
        LPC_GPIO->DIR[port] |= 1u << pin;
    else if (out == 0)
        LPC_GPIO->DIR[port] &= ~(1u << pin);
}

/* 0x48ba  callers: ~290
 * Write a pin by id (also forces it to output, see gpio_port_write). */
void gpio_wr(u32 id, u32 level)
{
    u8 port = (u8)rt_idivmod((s32)id >> 8, 3).rem;
    u8 pin  = (u8)id;

    gpio_port_write(port, pin, level);
}

/* 0x48d8  callers: 2 (I2C1 command task)
 * XOR checksum over the first `len` bytes of the frame that the pointer
 * i2c1_frame_ptr refers to, starting from `seed`. */
u32 cmd_xor_checksum(u32 len, u32 seed)
{
    u32 x = seed;
    u8 i;
    for (i = 0; (s32)i < (s32)len; i++)
        x ^= ((u8 *)i2c1_frame_ptr)[i];
    return x;
}

/* 0x48fc  callers: 7   getter */
u8 fn_48fc(void) { return g_02e6; }

/* 0x4908  callers: 1   getter */
u8 fn_4908(void) { return g_02e5; }

/* 0x4914  callers: 1   getter */
u8 fn_4914(void) { return uart4_flag; }

/* 0x4920  callers: 1   getter */
u8 fn_4920(void) { return uart4_data_len; }

/* 0x492c  callers: 5
 * True when the EEPROM answered at I2C address 0xAC at first probe. */
u8 eeprom_ac_ok(void) { return eeprom_ac_present; }

/* 0x4938  callers: 1 (power_off, 0x7428)
 * Point i2c0_buf_ptr at the I2C0 buffer. */
void i2c0_buf_ptr_init(void)
{
    i2c0_buf_ptr = get_i2c0_buf();
}

/* 0x4948  callers: 1
 * Clear the I2C0 slave receive count. */
void i2c0_rx_count_clear(void)
{
    i2c0_rx_count = 0;
}

/* 0x4954  callers: 1
 * Address of the I2C0 buffer. */
u8 *get_i2c0_buf(void)
{
    return i2c0_buf;
}

/* 0x495c  callers: 1
 * I2C0 slave receive count. */
u8 get_i2c0_rx_count(void)
{
    return i2c0_rx_count;
}

/* 0x4968  callers: 0 (vector: I2C0 IRQ)
 * I2C0 interrupt: combined master/slave state machine.
 * switch8 table at 0x4986: 26 entries + default, index = STAT >> 3.
 * A status with any of the low 3 bits set goes straight to the exit.
 * Every path ends by clearing SI. */
void i2c0_irq(void)
{
    u8 stat;

    i2c0_irq_seen = 1;
    stat = (u8)LPC_I2C0->STAT;
    if ((stat & 7) != 0)
        goto done;

    switch (stat >> 3) {
    /* ---------------- master ---------------- */
    case 1:     /* 0x08 START transmitted */
        i2c0_master_state = 1;
        i2c0_idx = 0;
        LPC_I2C0->DAT = i2c0_buf[i2c0_idx];         /* SLA+R/W */
        LPC_I2C0->CONSET = 0x04;                /* AA */
        LPC_I2C0->CONCLR = 0x20;                /* STA */
        i2c0_idx = i2c0_idx + 1;
        break;

    case 2:     /* 0x10 repeated START transmitted */
        i2c0_master_state = 2;
        i2c0_idx = 0;
        LPC_I2C0->DAT = i2c0_buf[i2c0_idx];
        LPC_I2C0->CONSET = 0x04;
        LPC_I2C0->CONCLR = 0x20;
        i2c0_idx = i2c0_idx + 1;
        break;

    case 3:     /* 0x18 SLA+W sent, ACK */
        if (i2c0_master_state == 1 || i2c0_master_state == 2) {
            i2c0_master_state = 4;
            LPC_I2C0->DAT = i2c0_buf[i2c0_idx];
            LPC_I2C0->CONSET = 0x04;
            i2c0_idx = i2c0_idx + 1;
        }
        break;

    case 4:     /* 0x20 SLA+W sent, NACK */
        i2c0_master_state = 5;
        LPC_I2C0->CONSET = 0x14;                /* STO | AA */
        i2c0_nack = 1;
        break;

    case 5:     /* 0x28 data sent, ACK */
        i2c0_master_state = 4;
        if (i2c0_idx < i2c0_tx_len) {
            LPC_I2C0->DAT = i2c0_buf[i2c0_idx];
            LPC_I2C0->CONSET = 0x04;
            i2c0_idx = i2c0_idx + 1;
        } else {
            LPC_I2C0->CONSET = 0x14;            /* STO | AA */
        }
        break;

    case 6:     /* 0x30 data sent, NACK */
        i2c0_master_state = 5;
        i2c0_idx = 0;
        LPC_I2C0->CONSET = 0x14;
        i2c0_nack = 1;
        break;

    case 7:     /* 0x38 arbitration lost */
        i2c0_master_state = 0;
        i2c0_idx = 0;
        LPC_I2C0->CONSET = 0x24;                /* STA | AA : retry */
        break;

    case 8:     /* 0x40 SLA+R sent, ACK */
        i2c0_master_state = 4;
        LPC_I2C0->CONSET = 0x04;
        break;

    case 9:     /* 0x48 SLA+R sent, NACK */
        i2c0_master_state = 5;
        LPC_I2C0->CONSET = 0x14;
        i2c0_nack = 1;
        break;

    case 10:    /* 0x50 data received, ACK returned.
                   The index really is zeroed on every byte, so each byte
                   lands in i2c0_buf[0] and the index is then 1. */
        i2c0_idx = 0;
        i2c0_buf[i2c0_idx] = (u8)LPC_I2C0->DAT;
        i2c0_idx = i2c0_idx + 1;
        if (i2c0_idx < i2c0_rx_count) {
            i2c0_master_state = 4;
            LPC_I2C0->CONSET = 0x04;
        } else {
            i2c0_master_state = 5;
            LPC_I2C0->CONCLR = 0x04;            /* NACK the next byte */
        }
        break;

    case 11:    /* 0x58 data received, NACK returned */
        i2c0_buf[i2c0_idx] = (u8)LPC_I2C0->DAT;
        i2c0_master_state = 5;
        LPC_I2C0->CONSET = 0x14;
        break;

    /* ---------------- slave receiver ---------------- */
    case 12:    /* 0x60 own SLA+W received, ACK returned */
    case 13:    /* 0x68 arbitration lost, own SLA+W received */
        i2c0_idx = 0;
        i2c0_rx_count = 0;
        i2c0_slave_state = 6;
        LPC_I2C0->CONSET = 0x04;
        break;

    case 16:    /* 0x80 slave data received, ACK returned */
        if (i2c0_slave_state == 6) {
            i2c0_buf[i2c0_idx] = (u8)LPC_I2C0->DAT;
            if (i2c0_idx < 0x24) {
                i2c0_idx = i2c0_idx + 1;
                i2c0_rx_count = i2c0_idx;
                /* re-map the first byte in place (I2C0 only) */
                if (i2c0_buf[0] == 0)
                    i2c0_buf[0] = 0x45;
                else if (i2c0_buf[0] == 1)
                    i2c0_buf[0] = 0x30;
                else if (i2c0_buf[0] == 2)
                    i2c0_buf[0] = 0x31;
            }
            LPC_I2C0->CONSET = 0x04;
        } else {
            LPC_I2C0->CONCLR = 0x04;
        }
        break;

    case 20:    /* 0xA0 STOP / repeated START while addressed as slave */
        LPC_I2C0->CONSET = 0x04;
        i2c0_slave_state = 0;
        break;

    /* ---------------- slave transmitter ---------------- */
    case 21:    /* 0xA8 own SLA+R received, ACK returned */
        i2c0_idx = 0;
        LPC_I2C0->DAT = i2c0_buf[i2c0_idx];
        i2c0_idx = i2c0_idx + 1;
        LPC_I2C0->CONSET = 0x04;
        i2c0_slave_state = 7;
        break;

    case 22:    /* 0xB0 arbitration lost, own SLA+R received */
        LPC_I2C0->CONSET = 0x04;
        break;

    case 23:    /* 0xB8 slave data sent, ACK received */
        if (i2c0_slave_state == 7 && i2c0_idx < i2c0_tx_len) {
            LPC_I2C0->DAT = i2c0_buf[i2c0_idx];
            if (i2c0_idx < 0x24)
                i2c0_idx = i2c0_idx + 1;
        }
        LPC_I2C0->CONSET = 0x04;
        break;

    case 24:    /* 0xC0 slave data sent, NACK received */
        LPC_I2C0->CONSET = 0x04;
        i2c0_slave_state = 5;
        break;

    case 25:    /* 0xC8 last slave data sent, ACK received */
        LPC_I2C0->CONSET = 0x04;
        break;

    case 0:     /* 0x00 bus error */
    case 14:    /* 0x70 general call received */
    case 15:    /* 0x78 arbitration lost, general call */
    case 17:    /* 0x88 slave data received, NACK returned */
    case 18:    /* 0x90 general-call data, ACK */
    case 19:    /* 0x98 general-call data, NACK */
    default:
        break;
    }
done:
    LPC_I2C0->CONCLR = 0x08;                    /* SI */
}

/* 0x4c80  callers: 1 (main loop, 0x891e)
 * I2C0 slave command task.  Runs once at least 2 bytes have been received
 * as slave; frame is cmd = i2c0_buf_ptr[0], arg = i2c0_buf_ptr[1].  IRQ 15 (I2C0) is
 * masked while the command executes.  Note the IRQ handler rewrites
 * first bytes 0/1/2 to 0x45/0x30/0x31, so those never match here. */
void i2c0_cmd_task(void)
{
    u8 n = get_i2c0_rx_count();

    if (n < 2)
        return;

    nvic_disable_irq_a(0xf);                   /* NVIC ICER: disable IRQ 15 */
    i2c0_rx_count_clear();                      /* rx count = 0 */

    switch (i2c0_buf_ptr[0]) {
    case 3:
        gpio_sel_p1_13_12(i2c0_buf_ptr[1]);         /* 2-bit selector P1_12/P1_13 */
        break;

    case 4:
        led_mode(i2c0_buf_ptr[1]);
        break;

    case 5:
        if (led_fault_latch == 0) {
            power_off_normal();
            power_clear_on_flags();
        } else {
            gpio_sel_p1_13_12(0);
            gpio_wr(P(1,19), 0);
            gpio_wr(P(1,8), 0);
            gpio_sel_p2_21_20_n(0);
            power_clear_on_flags();
            delay_ticks(35000);        /* 0x88b8 */
            if (key_wait_power_loop() != 0) {
                led_fault_latch = 0;
                power_off_normal();
                power_clear_on_flags();
            }
        }
        break;

    case 6:
        if (led_fault_latch == 0)
            power_on();
        break;

    default:
        break;
    }

    nvic_enable_irq_b(0xf);                   /* NVIC ISER: enable IRQ 15 */
}

/* 0x4d44  callers: 3 (i2c1_master_write and the two phases of i2c1_master_write_read)
 * Kick off an I2C1 master transfer: reset state, set STA.  Always returns 1.
 * The bytes to send (SLA first) and the length must already be in
 * i2c1_buf[] / i2c1_tx_len. */
u32 i2c1_master_start(void)
{
    i2c1_master_state = 0;
    i2c1_idx = 0;
    i2c1_error = 0;
    LPC_I2C1->CONSET = 0x20;        /* STA */
    return 1;
}

/* 0x4d6c  callers: 1
 * Clear the I2C1 slave receive count (frame consumed). */
void i2c1_rx_count_clear(void)
{
    i2c1_rx_count = 0;
}

/* 0x4d78  callers: 1
 * Address of the I2C1 buffer. */
u8 *get_i2c1_buf(void)
{
    return i2c1_buf;
}

/* 0x4d80  callers: 1 (I2C1 command task 0x2b70)
 * I2C1 slave receive count: number of bytes received so far. */
u8 get_i2c1_rx_count(void)
{
    return i2c1_rx_count;
}

/* 0x4d8c  callers: 18 (I2C1 command handlers)
 * Own I2C1 slave address register (ADR0), low byte. */
u8 i2c1_own_addr(void)
{
    return (u8)LPC_I2C1->ADR0;
}

/* 0x4d98  callers: 0 (vector: I2C1 IRQ)
 * I2C1 interrupt: the same combined master/slave state machine as i2c0_irq,
 * on I2C1 with its own variables.  Master side carries the EEPROM
 * transactions (i2c1_master_write / i2c1_master_write_read), slave side feeds the command task
 * 0x2b70.  The only logic difference from I2C0 is case 16: no re-mapping
 * of the first received byte.
 * switch8 table at 0x4db6: 26 entries + default, index = STAT >> 3.
 * (Case 21 is reached directly; the `b` at 0x4fa4 is an unused trampoline.) */
void i2c1_irq(void)
{
    u8 stat;

    i2c1_irq_seen = 1;
    stat = (u8)LPC_I2C1->STAT;
    if ((stat & 7) != 0)
        goto done;

    switch (stat >> 3) {
    /* ---------------- master ---------------- */
    case 1:     /* 0x08 START transmitted */
        i2c1_master_state = 1;
        i2c1_idx = 0;
        LPC_I2C1->DAT = i2c1_buf[i2c1_idx];         /* SLA+R/W */
        LPC_I2C1->CONSET = 0x04;                /* AA */
        LPC_I2C1->CONCLR = 0x20;                /* STA */
        i2c1_idx = i2c1_idx + 1;
        break;

    case 2:     /* 0x10 repeated START transmitted */
        i2c1_master_state = 2;
        i2c1_idx = 0;
        LPC_I2C1->DAT = i2c1_buf[i2c1_idx];
        LPC_I2C1->CONSET = 0x04;
        LPC_I2C1->CONCLR = 0x20;
        i2c1_idx = i2c1_idx + 1;
        break;

    case 3:     /* 0x18 SLA+W sent, ACK */
        if (i2c1_master_state == 1 || i2c1_master_state == 2) {
            i2c1_master_state = 4;
            LPC_I2C1->DAT = i2c1_buf[i2c1_idx];
            LPC_I2C1->CONSET = 0x04;
            i2c1_idx = i2c1_idx + 1;
        }
        break;

    case 4:     /* 0x20 SLA+W sent, NACK */
        i2c1_master_state = 5;
        LPC_I2C1->CONSET = 0x14;                /* STO | AA */
        i2c1_error = 1;
        break;

    case 5:     /* 0x28 data sent, ACK */
        i2c1_master_state = 4;
        if (i2c1_idx < i2c1_tx_len) {
            LPC_I2C1->DAT = i2c1_buf[i2c1_idx];
            LPC_I2C1->CONSET = 0x04;
            i2c1_idx = i2c1_idx + 1;
        } else {
            LPC_I2C1->CONSET = 0x14;            /* STO | AA */
        }
        break;

    case 6:     /* 0x30 data sent, NACK */
        i2c1_master_state = 5;
        i2c1_idx = 0;
        LPC_I2C1->CONSET = 0x14;
        i2c1_error = 1;
        break;

    case 7:     /* 0x38 arbitration lost */
        i2c1_master_state = 0;
        i2c1_idx = 0;
        LPC_I2C1->CONSET = 0x24;                /* STA | AA : retry */
        break;

    case 8:     /* 0x40 SLA+R sent, ACK */
        i2c1_master_state = 4;
        LPC_I2C1->CONSET = 0x04;
        break;

    case 9:     /* 0x48 SLA+R sent, NACK */
        i2c1_master_state = 5;
        LPC_I2C1->CONSET = 0x14;
        i2c1_error = 1;
        break;

    case 10:    /* 0x50 data received, ACK returned (index zeroed each time,
                   as in i2c0_irq) */
        i2c1_idx = 0;
        i2c1_buf[i2c1_idx] = (u8)LPC_I2C1->DAT;
        i2c1_idx = i2c1_idx + 1;
        if (i2c1_idx < i2c1_rx_count) {
            i2c1_master_state = 4;
            LPC_I2C1->CONSET = 0x04;
        } else {
            i2c1_master_state = 5;
            LPC_I2C1->CONCLR = 0x04;            /* NACK the next byte */
        }
        break;

    case 11:    /* 0x58 data received, NACK returned */
        i2c1_buf[i2c1_idx] = (u8)LPC_I2C1->DAT;
        i2c1_master_state = 5;
        LPC_I2C1->CONSET = 0x14;
        break;

    /* ---------------- slave receiver ---------------- */
    case 12:    /* 0x60 own SLA+W received, ACK returned */
    case 13:    /* 0x68 arbitration lost, own SLA+W received */
        i2c1_idx = 0;
        i2c1_rx_count = 0;
        i2c1_slave_state = 6;
        LPC_I2C1->CONSET = 0x04;
        break;

    case 16:    /* 0x80 slave data received, ACK returned */
        if (i2c1_slave_state == 6) {
            i2c1_buf[i2c1_idx] = (u8)LPC_I2C1->DAT;
            if (i2c1_idx < 0x24) {
                i2c1_idx = i2c1_idx + 1;
                i2c1_rx_count = i2c1_idx;
            }
            LPC_I2C1->CONSET = 0x04;
        } else {
            LPC_I2C1->CONCLR = 0x04;
        }
        break;

    case 20:    /* 0xA0 STOP / repeated START while addressed as slave */
        LPC_I2C1->CONSET = 0x04;
        i2c1_slave_state = 0;
        break;

    /* ---------------- slave transmitter ---------------- */
    case 21:    /* 0xA8 own SLA+R received, ACK returned */
        i2c1_idx = 0;
        LPC_I2C1->DAT = i2c1_buf[i2c1_idx];
        i2c1_idx = i2c1_idx + 1;
        LPC_I2C1->CONSET = 0x04;
        i2c1_slave_state = 7;
        break;

    case 22:    /* 0xB0 arbitration lost, own SLA+R received */
        LPC_I2C1->CONSET = 0x04;
        break;

    case 23:    /* 0xB8 slave data sent, ACK received */
        if (i2c1_slave_state == 7 && i2c1_idx < i2c1_tx_len) {
            LPC_I2C1->DAT = i2c1_buf[i2c1_idx];
            if (i2c1_idx < 0x24)
                i2c1_idx = i2c1_idx + 1;
        }
        LPC_I2C1->CONSET = 0x04;
        break;

    case 24:    /* 0xC0 slave data sent, NACK received */
        LPC_I2C1->CONSET = 0x04;
        i2c1_slave_state = 5;
        break;

    case 25:    /* 0xC8 last slave data sent, ACK received */
        LPC_I2C1->CONSET = 0x04;
        break;

    case 0:     /* 0x00 bus error */
    case 14:    /* 0x70 general call received */
    case 15:    /* 0x78 arbitration lost, general call */
    case 17:    /* 0x88 slave data received, NACK returned */
    case 18:    /* 0x90 general-call data, ACK */
    case 19:    /* 0x98 general-call data, NACK */
    default:
        break;
    }
done:
    LPC_I2C1->CONCLR = 0x08;                    /* SI */
}

/* 0x504e  "FUNC" in app.txt is not code: it is the tail of i2c1_irq's literal
 * pool (0x504c .. 0x506f): 0x1000017c, 0x40020000, 0x1000016c, 0x10000177,
 * 0x100005ba, 0x1000017d, 0x10000179, 0x10000178, 0x10000170. */

/* 0x5070  callers: 1 (eeprom_write_byte, 0x6584)
 * I2C1 master write: send `sla` followed by `len` bytes from `data`, wait a
 * fixed 200 ms, then report.  Returns 1 on success, 3 if the start failed
 * (cannot happen: i2c1_master_start always returns 1) or a NACK was flagged. */
u32 i2c1_master_write(u8 sla, const u8 *data, u8 len)
{
    u32 r = 0;                      /* kept on the stack in the original */
    u8 i;

    i2c1_tx_len = len + 1;               /* tx length including address byte */
    i2c1_rx_count = 0;
    i2c1_buf[0] = sla;
    for (i = 0; i < len; i++)
        i2c1_buf[i + 1] = data[i];

    r = i2c1_master_start();
    if (r == 0)
        return 3;
    delay_ticks(200);
    if (i2c1_error != 0)
        return 3;
    return r;
}


/* ======================================================================
 * Part 4
 * ====================================================================== */

/*
 * part_4.c -- LPC11U67 application, 0x50d4 <= addr < 0x613c
 *
 * Contents: I2C1 master read helper, IR (NEC) decoder + result getters,
 * pin-interrupt setup, I2C0/I2C1 init, clock/PLL init, USART0..4 init,
 * factory key/IR wait loop, power-key debounce, LED blink/breathe engine,
 * USART1 receive task.
 *
 * Helpers outside this range that are used here (read to confirm):
 *   nvic_disable_irq_a(n), nvic_disable_irq(n)            NVIC->ICER = 1 << (n & 31)   (IRQ disable)
 *   nvic_enable_irq_a(n), nvic_enable_irq_b(n), nvic_enable_irq(n) NVIC->ISER = 1 << (n & 31)  (IRQ enable)
 *   rt_uidiv(a, b)                     unsigned divide a / b (Keil __aeabi_uidiv entry) / * ? * /
 *   rt_idivmod(a, b)                 quotient in r0, remainder in r1
 *   i2c1_master_start()                         i2c1_master_state = 0; i2c1_idx = 0; i2c1_error = 0;
 *                                     LPC_I2C1->CONSET = 0x20 (STA); return 1
 *   timer_timeout_get(ch)                       return ir_tick[ch]  (u16 IR tick counter)
 *   timer_timeout_reset(ch)                       ir_tick[ch] = 0
 *   timer_ct32b1_count_reset()                         ct32b1_tick_count = 0 (u16 LED phase tick counter)
 *   rt_nop(x)                        empty (bx lr)
 *   power_get_state_code()                         return power_state_code
 *   get_uart1_rx_count()                         return uart1_rx_count (u32 USART1 rx byte count)
 *   get_uart1_rx_flag()                         return uart1_rx_flag (u8 USART1 rx error/overflow flag ?)
 *   uart1_rx_reset()                         IRQ11 off; uart1_rx_count = 0; uart1_rx_flag = 0; IRQ11 on
 *   timer_soft4_get()                         return timer_soft[4]  (u32 at 0x100005f0, ms counter ?)
 *   timer_soft4_reset()                         timer_soft[4] = 0
 *
 * IR decoder state (all "arrays" have exactly ONE element: the code is
 * written as loops over `ch < 1`, so they overlap-free pack like this):
 *   0x10000184 u16 ir_shift[1]  shift register (LSB first, new bit enters at bit 15)
 *   0x10000186 u8  ir_code[1]  decoded command byte, 0 = none (read by ir_take_code)
 *   0x10000187 u8  ir_frame_done[1]  0xff once a complete 32-bit frame has timed out
 *   0x10000188 u16 ir_interval[1]  ticks since previous falling edge
 *   0x1000018a u8  ir_bit_count[1]  bit counter
 *   0x1000018b u8  ir_receiving[1]  1 = leader seen, receiving
 *   0x1000018c u8  ir_idle[1]  1 = inter-frame timeout happened ("IR idle" event)
 *   0x1000018d u8  ir_addr_is_ffff     1 = address word was 0xffff, 0 = it was 0x504f
 */

/* 0x50d4  callers: 1
 * I2C1 master "write wlen bytes, then read rlen bytes" transaction used by
 * eeprom_read_byte (0x64f4).  NOTE: the START is issued on I2C1 (i2c1_master_start
 * writes LPC_I2C1->CONSET), not I2C0.  Shared ISR state:
 *   i2c1_tx_len = number of bytes to transmit (incl. address byte)
 *   i2c1_rx_count = number of bytes to receive
 *   i2c1_buf = transfer buffer, [0] = device address byte
 *   i2c1_error = error flag set by the ISR
 * Returns 3 = first START failed, 4 = second START failed or ISR error,
 * otherwise the (non-zero) result of i2c1_master_start, i.e. 1.
 * i2c1_master_start always returns 1, so the 3/4 "start failed" paths are dead. */
u32 i2c1_master_write_read(u8 dev, u8 *wbuf, u8 wlen, u8 *rbuf, /* stack */ u32 rlen)
{
    u8 r = 0;
    u8 i;

    i2c1_tx_len = wlen + 1;
    i2c1_rx_count = 0;
    i2c1_buf[0] = dev;
    for (i = 0; i < wlen; i++)
        i2c1_buf[i + 1] = wbuf[i];

    r = i2c1_master_start();
    if (r == 0)
        return 3;

    delay_ticks(10);

    i2c1_tx_len = 1;
    i2c1_rx_count = (u8)rlen;
    i2c1_buf[0] = dev | 1;
    r = i2c1_master_start();
    if (r == 0)
        return 4;

    delay_ticks(200);

    for (i = 0; i < rlen; i++)
        rbuf[i] = i2c1_buf[i];          /* sic: read back from index 0 of the same buffer */

    if (i2c1_error != 0)
        return 4;
    return r;
}

/* 0x5178  callers: 1
 * Empty stub (bx lr).  Called with 0 from the I2C1 command switch. */
void i2c1_stub_nop(u32 unused)
{
}

/* 0x517c  callers: 1
 * Set the I2C1 transmit length with the I2C1 interrupt (IRQ 10) masked. */
void i2c1_set_tx_len(u8 len)
{
    nvic_disable_irq_a(10);                      /* NVIC disable IRQ10 (I2C1) */
    i2c1_tx_len = len;
    nvic_enable_irq_b(10);                      /* NVIC enable  IRQ10 */
}

/* 0x5198  callers: 1
 * Take-and-clear ir_frame_done[] (the "complete frame ended" marker, 0xff or 0).
 * Returns the first non-zero entry, then zeroes all (one) entries. */
u8 ir_take_frame_done(void)
{
    u8 v = 0;
    u8 ch;

    for (ch = 0; ch < 1; ch++) {
        if (ir_frame_done[ch] != 0) {
            v = ir_frame_done[ch];
            break;
        }
    }
    for (ch = 0; ch < 1; ch++)
        ir_frame_done[ch] = 0;
    return v;
}

/* 0x51d0  callers: 7
 * Take-and-clear the decoded IR command byte.  0 = nothing pending.
 * (main treats 2 as the power key.) */
u8 ir_take_code(void)
{
    u8 code = 0;
    u8 ch;

    for (ch = 0; ch < 1; ch++) {
        code = ir_code[ch];
        if (code != 0)
            break;
    }
    if (code == 0)
        return 0;
    for (ch = 0; ch < 1; ch++)
        ir_code[ch] = 0;
    return code;
}

/* 0x520c  callers: 1
 * IR inter-edge timeout in ticks (147).  Stored into ir_timeout_ticks by the timer
 * init at 0x7f72 and compared against ir_tick[ch] in the timer IRQ. */
u32 get_ir_timeout_ticks(void)
{
    return 0x93;
}

/* 0x5210  callers: 1
 * IR timeout, called from the timer IRQ (0x7c78) when ir_tick[ch] > ir_timeout_ticks,
 * i.e. no edge for more than 147 ticks.  Ends reception; if exactly 32 data
 * bits had been received (bit counter == 0x21) marks the frame as complete. */
void ir_on_timeout(u8 ch)
{
    ir_receiving[ch] = 0;
    if (ir_bit_count[ch] == 0x21)
        ir_frame_done[ch] = 0xff;
    ir_idle[ch] = 1;
    ir_shift[ch] = 0;
    ir_bit_count[ch] = 0;
}

/* 0x524c  callers: 1
 * IR falling-edge handler (called with ch = 0 from the PINT3 handler at
 * 0x682c).  NEC-style pulse-distance decoding on the interval between
 * consecutive falling edges, in timer ticks (apparently ~0.1 ms / tick ?):
 *   110..147  leader (13.5 ms)  -> start a frame
 *     8..13   bit 0  (1.125 ms)
 *    16..28   bit 1  (2.25 ms)
 *   > 147     abort
 * After 16 bits the address word must be 0x504f (bytes 0x4f,0x50) or 0xffff;
 * after 32 bits the low byte of the second word is the command and
 * (command | inverse) must be 0xff. */
void ir_on_edge(u8 ch)
{
    ir_interval[ch] = timer_timeout_get(ch);                     /* ticks since last edge */

    if (ir_interval[ch] > 0x93) {
        ir_receiving[ch] = 0;
        ir_shift[ch] = 0;
        ir_bit_count[ch] = 0;
    } else if (ir_interval[ch] >= 0x6e && ir_interval[ch] <= 0x93) {
        ir_receiving[ch] = 1;                           /* leader */
        ir_shift[ch] = 0;
        ir_bit_count[ch] = 0;
    }

    if (ir_receiving[ch] == 1) {
        if (ir_interval[ch] < 0xe && ir_interval[ch] > 7) {
            ir_shift[ch] = ir_shift[ch] >> 1;                         /* bit 0 */
        } else if (ir_interval[ch] < 0x1d && ir_interval[ch] > 0xf) {
            ir_shift[ch] = ir_shift[ch] >> 1;                         /* bit 1 */
            ir_shift[ch] = ir_shift[ch] | 0x8000;
        }

        if (ir_bit_count[ch] == 0x10) {
            /* 16 bits in: address word */
            if (ir_shift[ch] != 0x504f && ir_shift[ch] != 0xffff) {
                ir_bit_count[ch] = 0;
                ir_receiving[ch] = 0;
            }
            if (ir_shift[ch] == 0xffff)
                ir_addr_is_ffff = 1;
            else
                ir_addr_is_ffff = 0;
            ir_shift[ch] = 0;
        } else if (ir_bit_count[ch] == 0x20) {
            /* 32 bits in: command, ~command */
            ir_code[ch] = (u8)ir_shift[ch];
            if ((ir_code[ch] | (ir_shift[ch] >> 8)) != 0xff)
                ir_code[ch] = 0;
            ir_shift[ch] = 0;
            ir_frame_done[ch] = 0;
        } else if (ir_bit_count[ch] >= 0x30) {
            if (ir_bit_count[ch] == 0x30) {
                /* 48 bits in (not reachable with a 32-bit remote ?) */
                ir_frame_done[ch] = (u8)ir_shift[ch];
                if ((ir_frame_done[ch] | (ir_shift[ch] >> 8)) != 0xff)
                    ir_code[ch] = 0;               /* sic: clears ir_code, not ir_frame_done */
            }
            ir_shift[ch] = 0;
            ir_bit_count[ch] = 0;
        }
        ir_bit_count[ch] = ir_bit_count[ch] + 1;
    }

    timer_timeout_reset(ch);                                  /* restart the tick counter */
}

/* 0x53f4  callers: 2
 * IR init: route P1_1 to pin interrupt 3, falling edge, and clear the
 * decoder state. */
void ir_init(void)
{
    u8 ch;

    gpio_pint_config(3, 1, 1, 0, 0);                       /* PINT3 <- P1_1, edge, falling */
    for (ch = 0; ch < 1; ch++) {
        ir_shift[ch] = 0;
        ir_bit_count[ch] = 0;
        ir_code[ch] = 0;
        ir_frame_done[ch] = 0;
        ir_receiving[ch] = 0;
        ir_idle[ch] = 0;
    }
}

/* 0x5448  callers: 2
 * Return the "IR timed out / idle" flag ir_idle[]; clear it if `clear`. */
u8 ir_idle_flag(u8 clear)
{
    u8 v = 0;
    u8 ch;

    for (ch = 0; ch < 1; ch++) {
        v = ir_idle[ch];
        if (v != 0)
            break;
    }
    if (clear != 0) {
        for (ch = 0; ch < 1; ch++)
            ir_idle[ch] = 0;
    }
    return v;
}

/* 0x5484  callers: 1
 * Configure a GPIO pin interrupt.
 *   pint   0..7   pin interrupt channel (also the NVIC IRQ number)
 *   port,pin      source pin; PINTSEL value is pin (P0), pin+24 (P1), pin+56 (P2)
 *   level  0 = edge sensitive, else level sensitive
 *   pol    edge: 0 = falling, else rising;  level: 0 = active low, else active high
 * The 8-way switch (rt_switch8 table at 0x54a4: 08 05 1c 33 4a 61 78 8f a6 bd)
 * is identical per case apart from the PINTSEL index. */
void gpio_pint_config(u8 pint, u32 port, u32 pin, u32 level, /* stack */ u32 pol)
{
    LPC_SYSCON->SYSAHBCLKCTRL |= 1 << 19;         /* PINT clock */

    switch (pint) {
    case 0:
        if (port == 1)      LPC_SYSCON->PINTSEL[0] = pin + 0x18;
        else if (port == 2) LPC_SYSCON->PINTSEL[0] = pin + 0x38;
        else                LPC_SYSCON->PINTSEL[0] = pin;
        nvic_enable_irq_a(0);                               /* NVIC enable IRQ0 */
        break;
    case 1:
        if (port == 1)      LPC_SYSCON->PINTSEL[1] = pin + 0x18;
        else if (port == 2) LPC_SYSCON->PINTSEL[1] = pin + 0x38;
        else                LPC_SYSCON->PINTSEL[1] = pin;
        nvic_enable_irq_a(1);
        break;
    case 2:
        if (port == 1)      LPC_SYSCON->PINTSEL[2] = pin + 0x18;
        else if (port == 2) LPC_SYSCON->PINTSEL[2] = pin + 0x38;
        else                LPC_SYSCON->PINTSEL[2] = pin;
        nvic_enable_irq_a(2);
        break;
    case 3:
        if (port == 1)      LPC_SYSCON->PINTSEL[3] = pin + 0x18;
        else if (port == 2) LPC_SYSCON->PINTSEL[3] = pin + 0x38;
        else                LPC_SYSCON->PINTSEL[3] = pin;
        nvic_enable_irq_a(3);
        break;
    case 4:
        if (port == 1)      LPC_SYSCON->PINTSEL[4] = pin + 0x18;
        else if (port == 2) LPC_SYSCON->PINTSEL[4] = pin + 0x38;
        else                LPC_SYSCON->PINTSEL[4] = pin;
        nvic_enable_irq_a(4);
        break;
    case 5:
        if (port == 1)      LPC_SYSCON->PINTSEL[5] = pin + 0x18;
        else if (port == 2) LPC_SYSCON->PINTSEL[5] = pin + 0x38;
        else                LPC_SYSCON->PINTSEL[5] = pin;
        nvic_enable_irq_a(5);
        break;
    case 6:
        if (port == 1)      LPC_SYSCON->PINTSEL[6] = pin + 0x18;
        else if (port == 2) LPC_SYSCON->PINTSEL[6] = pin + 0x38;
        else                LPC_SYSCON->PINTSEL[6] = pin;
        nvic_enable_irq_a(6);
        break;
    case 7:
        if (port == 1)      LPC_SYSCON->PINTSEL[7] = pin + 0x18;
        else if (port == 2) LPC_SYSCON->PINTSEL[7] = pin + 0x38;
        else                LPC_SYSCON->PINTSEL[7] = pin;
        nvic_enable_irq_a(7);
        break;
    default:
        break;
    }

    if (level == 0) {
        LPC_PINT->ISEL &= ~(1 << pint);           /* 0xa0004000: edge sensitive */
        if (pol == 0)
            LPC_PINT->IENF |= 1 << pint;          /* +0x10: falling edge enable */
        else
            LPC_PINT->IENR |= 1 << pint;          /* +0x04: rising edge enable */
    } else {
        LPC_PINT->ISEL |= 1 << pint;              /* level sensitive */
        LPC_PINT->IENR |= 1 << pint;              /* level interrupt enable */
        if (pol == 0)
            LPC_PINT->IENF &= ~(1 << pint);       /* active low */
        else
            LPC_PINT->IENF |= 1 << pint;          /* active high */
    }
}

/* 0x56a4  callers: 1
 * I2C0 init: pins P0_4 (SCL) / P0_5 (SDA), own slave address 0x66,
 * SCLH = SCLL = 240 (100 kHz at 48 MHz), IRQ 15, interface enabled with ACK. */
void i2c0_init(void)
{
    LPC_SYSCON->PRESETCTRL    |= 2;               /* de-assert I2C0 reset */
    LPC_SYSCON->SYSAHBCLKCTRL |= 0x20;            /* I2C0 clock */

    LPC_IOCON->PIO0_4 &= ~7;                      /* 0x40044010 */
    LPC_IOCON->PIO0_4 |= 1;                       /* func 1 = I2C0_SCL */
    LPC_IOCON->PIO0_5 &= ~7;                      /* 0x40044014 */
    LPC_IOCON->PIO0_5 |= 1;                       /* func 1 = I2C0_SDA */

    LPC_I2C0->CONCLR = 0x6c;                      /* 0x40000018: AA|SI|STA|I2EN */
    LPC_I2C0->SCLL   = 0xf0;
    LPC_I2C0->SCLH   = 0xf0;
    LPC_I2C0->ADR0   = 0x66;
    nvic_enable_irq_b(15);                                  /* NVIC enable IRQ15 (I2C0) */
    LPC_I2C0->CONSET = 0x44;                      /* I2EN | AA */

    i2c0_idx = 0;
    i2c0_rx_count = 0;
    i2c0_tx_len = 0;
}

/* 0x5730  callers: 1
 * I2C1 init: pins P1_11 (SCL) / P1_14 (SDA), own slave address 0xb0,
 * SCLH = SCLL = 240, IRQ 10, interface enabled with ACK. */
void i2c1_init(void)
{
    LPC_SYSCON->PRESETCTRL    |= 8;               /* de-assert I2C1 reset */
    LPC_SYSCON->SYSAHBCLKCTRL |= 1 << 25;         /* I2C1 clock */

    LPC_IOCON->PIO1_11 &= ~7;                     /* 0x4004408c */
    LPC_IOCON->PIO1_11 |= 1;                      /* func 1 = I2C1_SCL */
    LPC_IOCON->PIO1_14 &= ~7;                     /* 0x40044098 */
    LPC_IOCON->PIO1_14 |= 1;                      /* func 1 = I2C1_SDA */

    LPC_I2C1->CONCLR = 0x6c;                      /* 0x40020018 */
    LPC_I2C1->SCLL   = 0xf0;
    LPC_I2C1->SCLH   = 0xf0;
    LPC_I2C1->ADR0   = 0xb0;
    nvic_enable_irq_b(10);                                  /* NVIC enable IRQ10 (I2C1) */
    LPC_I2C1->CONSET = 0x44;                      /* I2EN | AA */

    i2c1_idx = 0;
    i2c1_rx_count = 0;
    i2c1_tx_len = 0;
}

/* 0x57c0  callers: 2
 * System clock init: crystal oscillator on P2_0/P2_1 -> PLL -> main clock.
 * Called as clock_init(1, 1, 3, 1): PLL source = system oscillator,
 * SYSPLLCTRL = 0x23 (MSEL = 3 -> x4, PSEL = 1), AHB divider 1.
 * With a 12 MHz crystal that is 48 MHz, matching the 48000000 constant
 * in the UART inits. */
void clock_init(u32 pll_src, u32 psel, u32 msel, u32 ahb_div)
{
    u16 i;

    LPC_SYSCON->SYSAHBCLKCTRL |= 1 << 16;         /* IOCON clock */
    LPC_IOCON->PIO2_0 = 1;                        /* 0x400440f0: XTALIN  */
    LPC_IOCON->PIO2_1 = 1;                        /* 0x400440f4: XTALOUT */
    LPC_SYSCON->SYSOSCCTRL = LPC_SYSCON->SYSOSCCTRL;   /* 0x40048020, read then write back */
    LPC_SYSCON->PDRUNCFG &= ~0x20;                /* 0x40048238: power up system oscillator */
    for (i = 0; i < 0x2500; i++)
        ;

    LPC_SYSCON->SYSPLLCLKSEL = pll_src;           /* 0x40048040 */
    LPC_SYSCON->SYSPLLCLKUEN = 1;                 /* 0x40048044 */
    LPC_SYSCON->SYSPLLCLKUEN = 0;
    LPC_SYSCON->SYSPLLCLKUEN = 1;
    while ((LPC_SYSCON->SYSPLLCLKUEN & 1) == 0)
        ;

    LPC_SYSCON->SYSPLLCTRL = (psel << 5) | msel;  /* 0x40048008 */
    LPC_SYSCON->PDRUNCFG &= ~0x80;                /* power up system PLL */
    while ((LPC_SYSCON->SYSPLLSTAT & 1) == 0)     /* 0x4004800c: wait for lock */
        ;

    LPC_SYSCON->MAINCLKSEL = 3;                   /* 0x40048070: PLL output */
    LPC_SYSCON->MAINCLKUEN = 1;                   /* 0x40048074 */
    LPC_SYSCON->MAINCLKUEN = 0;
    LPC_SYSCON->MAINCLKUEN = 1;
    while ((LPC_SYSCON->MAINCLKUEN & 1) == 0)
        ;

    LPC_SYSCON->SYSAHBCLKDIV = ahb_div;           /* 0x40048078 */
}

/* 0x588c  callers: 2
 * USART0 (16550-style) init, 8N1.
 * Pins: P0_18 = U0_RXD, P0_19 = U0_TXD (IOCON func 1).
 * Divisor = 48000000 / USART0CLKDIV(1) / 16 / baud  -> 156 (DLL 0x9c, DLM 0)
 * for 19200 (exact 19230.8).  RX interrupt only, IRQ 21. */
void uart0_init(u32 baud)
{
    u32 clkdiv, pclk16, div, dummy;

    uart0_tx_empty = 1;
    uart0_rx_count = 0;                                   /* u32 rx count */
    nvic_disable_irq(21);                                  /* NVIC disable IRQ21 (USART0) */

    LPC_IOCON->PIO0_18 &= ~7;                     /* 0x40044048 */
    LPC_IOCON->PIO0_18 |= 1;                      /* U0_RXD */
    LPC_IOCON->PIO0_19 &= ~7;                     /* 0x4004404c */
    LPC_IOCON->PIO0_19 |= 1;                      /* U0_TXD */

    LPC_SYSCON->SYSAHBCLKCTRL |= 1 << 12;         /* USART0 clock */
    LPC_SYSCON->USART0CLKDIV = 1;                 /* 0x40048098 */

    LPC_USART0->LCR = 0x83;                       /* 8N1, DLAB = 1 */
    clkdiv = LPC_SYSCON->USART0CLKDIV;
    pclk16 = rt_uidiv(48000000, clkdiv) >> 4;
    div    = rt_uidiv(pclk16, baud);
    LPC_USART0->DLM = div >> 8;
    LPC_USART0->DLL = div & 0xff;
    LPC_USART0->LCR = 3;                          /* 8N1, DLAB = 0 */
    LPC_USART0->FCR = 7;                          /* enable + reset both FIFOs */

    dummy = LPC_USART0->LSR;                      /* clear line status */
    do {
        (void)LPC_USART0->LSR;
    } while (0);                                  /* loop condition compiled to constant false */
    while (LPC_USART0->LSR & 1)                   /* drain receiver */
        dummy = LPC_USART0->RBR;

    nvic_enable_irq(21);                                  /* NVIC enable IRQ21 */
    LPC_USART0->IER = 1;                          /* RBR interrupt */
}

/* 0x595c  callers: 3
 * USART1 (LPC8xx-style USART) init, 8N1.
 * Pins: P0_13 = U1_RXD, P0_14 = U1_TXD (IOCON func 4).
 * Clock: FRGCLKDIV = 2, fractional generator off (DIV 0xff, MULT 0).
 * BRG = 48000000 / 2 / 16 / baud - 1  -> 77 for 19200 (exact 19230.8).
 * RXRDY interrupt, shared IRQ 11 (USART1/4). */
void uart1_init(u32 baud)
{
    u32 frgdiv = 2;
    s32 clk;
    u32 div;

    nvic_disable_irq(11);                                  /* NVIC disable IRQ11 */

    LPC_IOCON->PIO0_13 &= ~7;                     /* 0x40044034 */
    LPC_IOCON->PIO0_13 |= 4;                      /* U1_RXD */
    LPC_IOCON->PIO0_14 &= ~7;                     /* 0x40044038 */
    LPC_IOCON->PIO0_14 |= 4;                      /* U1_TXD */

    LPC_SYSCON->FRGCLKDIV = frgdiv;               /* 0x400480a0 */
    LPC_SYSCON->SYSAHBCLKCTRL |= 1 << 20;         /* USART1 clock */
    LPC_SYSCON->PRESETCTRL &= ~0x20;              /* pulse USART1 reset */
    LPC_SYSCON->PRESETCTRL |= 0x20;

    LPC_USART1->CFG = 4;                          /* 0x4006c000: 8 data bits, no parity, 1 stop */
    clk = rt_idivmod(48000000, frgdiv);
    div = rt_uidiv(clk / 16, baud);                /* signed /16 */
    LPC_USART1->BRG = div - 1;                    /* +0x20 */

    LPC_SYSCON->UARTFRGDIV  = 0xff;               /* 0x400480f0 */
    LPC_SYSCON->UARTFRGMULT = 0;                  /* 0x400480f4 */

    nvic_enable_irq(11);                                  /* NVIC enable IRQ11 */
    LPC_USART1->INTENSET |= 1;                    /* +0x0c: RXRDY */
    LPC_USART1->CFG |= 1;                         /* enable */
}

/* 0x5a24  callers: 1
 * USART2 init, 8N1, same scheme as USART1.
 * Pins: P1_0 (IOCON func 3) = U2_TXD, P1_6 (IOCON func 2) = U2_RXD.
 *       (direction per the LPC11U6x pin table, from memory) / * ? * /
 * BRG = 77 for 19200.  Shared IRQ 12 (USART2/3). */
void uart2_init(u32 baud)
{
    u32 frgdiv = 2;
    s32 clk;
    u32 div;

    nvic_disable_irq(12);                                  /* NVIC disable IRQ12 */

    LPC_IOCON->PIO1_0 &= ~7;                      /* 0x40044060 */
    LPC_IOCON->PIO1_0 |= 3;                       /* U2_TXD */
    LPC_IOCON->PIO1_6 &= ~7;                      /* 0x40044078 */
    LPC_IOCON->PIO1_6 |= 2;                       /* U2_RXD */

    LPC_SYSCON->FRGCLKDIV = frgdiv;
    LPC_SYSCON->SYSAHBCLKCTRL |= 1 << 21;         /* USART2 clock */
    LPC_SYSCON->PRESETCTRL &= ~0x40;              /* pulse USART2 reset */
    LPC_SYSCON->PRESETCTRL |= 0x40;

    LPC_USART2->CFG = 4;                          /* 0x40070000 */
    clk = rt_idivmod(48000000, frgdiv);
    div = rt_uidiv(clk / 16, baud);
    LPC_USART2->BRG = div - 1;

    LPC_SYSCON->UARTFRGDIV  = 0xff;
    LPC_SYSCON->UARTFRGMULT = 0;

    nvic_enable_irq(12);                                  /* NVIC enable IRQ12 */
    LPC_USART2->INTENSET |= 1;                    /* RXRDY */
    LPC_USART2->CFG |= 1;                         /* enable */
}

/* 0x5af4  callers: 1
 * USART3 init, 8N1, same scheme.
 * Pins: P2_3 = U3_RXD, P2_4 = U3_TXD (IOCON func 1).
 * BRG = 77 for 19200.  Shared IRQ 12 (USART2/3). */
void uart3_init(u32 baud)
{
    u32 frgdiv = 2;
    s32 clk;
    u32 div;

    nvic_disable_irq(12);                                  /* NVIC disable IRQ12 */

    LPC_IOCON->PIO2_3 &= ~7;                      /* 0x40044100 */
    LPC_IOCON->PIO2_3 |= 1;                       /* U3_RXD */
    LPC_IOCON->PIO2_4 &= ~7;                      /* 0x40044104 */
    LPC_IOCON->PIO2_4 |= 1;                       /* U3_TXD */

    LPC_SYSCON->FRGCLKDIV = frgdiv;
    LPC_SYSCON->SYSAHBCLKCTRL |= 1 << 22;         /* USART3/4 clock */
    LPC_SYSCON->PRESETCTRL &= ~0x80;              /* pulse USART3 reset */
    LPC_SYSCON->PRESETCTRL |= 0x80;

    LPC_USART3->CFG = 4;                          /* 0x40074000 */
    clk = rt_idivmod(48000000, frgdiv);
    div = rt_uidiv(clk / 16, baud);
    LPC_USART3->BRG = div - 1;

    LPC_SYSCON->UARTFRGDIV  = 0xff;
    LPC_SYSCON->UARTFRGMULT = 0;

    nvic_enable_irq(12);                                  /* NVIC enable IRQ12 */
    LPC_USART3->INTENSET |= 1;                    /* RXRDY */
    LPC_USART3->CFG |= 1;                         /* enable */
}

/* 0x5bbc  callers: 1
 * USART4 init, 8N1, same scheme.
 * Pins: P2_11 = U4_RXD, P2_12 = U4_TXD (IOCON func 1).
 * BRG = 77 for 19200.  Shared IRQ 11 (USART1/4). */
void uart4_init(u32 baud)
{
    u32 frgdiv = 2;
    s32 clk;
    u32 div;

    nvic_disable_irq(11);                                  /* NVIC disable IRQ11 */

    LPC_IOCON->PIO2_11 &= ~7;                     /* 0x40044120 */
    LPC_IOCON->PIO2_11 |= 1;                      /* U4_RXD */
    LPC_IOCON->PIO2_12 &= ~7;                     /* 0x40044124 */
    LPC_IOCON->PIO2_12 |= 1;                      /* U4_TXD */

    LPC_SYSCON->FRGCLKDIV = frgdiv;
    LPC_SYSCON->SYSAHBCLKCTRL |= 1 << 22;         /* USART3/4 clock */
    LPC_SYSCON->PRESETCTRL &= ~0x100;             /* pulse USART4 reset */
    LPC_SYSCON->PRESETCTRL |= 0x100;

    LPC_USART4->CFG = 4;                          /* 0x4004c000 */
    clk = rt_idivmod(48000000, frgdiv);
    div = rt_uidiv(clk / 16, baud);
    LPC_USART4->BRG = div - 1;

    LPC_SYSCON->UARTFRGDIV  = 0xff;
    LPC_SYSCON->UARTFRGMULT = 0;

    nvic_enable_irq(11);                                  /* NVIC enable IRQ11 */
    LPC_USART4->INTENSET |= 1;                    /* RXRDY */
    LPC_USART4->CFG |= 1;                         /* enable */
}

/* 0x5c84  callers: 3
 * Blocking wait loop (factory / burn-in style ?).  Re-inits the IR decoder,
 * then spins servicing the USART2 and USART1 tasks until one of:
 *   - the power key (P1_16 low) is seen on 10 consecutive passes, each
 *     pass followed by delay_ticks(10000);
 *   - IR code 2 (power) is seen within 1000 polls on 10 consecutive passes,
 *     each followed by delay_ticks(9500);
 *   - wait_loop_exit is set by someone else (it is cleared on exit).
 * Always returns 1. */
u32 key_wait_power_loop(void)
{
    u16 ir_polls   = 1000;
    u8  key_left   = 10;
    u8  ir_left    = 10;
    u8  key_down   = 0;

    ir_init();
    for (;;) {
        if (gpio_rd(P(1,16)) == 0) {
            delay_ticks(10000);
            key_down = 1;
            if (--key_left == 0)
                break;
        } else {
            key_down = 0;
            key_left = 10;
        }

        if (key_down == 0) {
            while (ir_polls != 0) {
                if (ir_take_code() == 2)
                    break;
                ir_polls--;
            }
            if (ir_polls != 0) {                  /* got IR code 2 */
                delay_ticks(9500);
                if (--ir_left == 0)
                    break;
            } else {
                ir_left = 10;
            }
            ir_polls = 1000;
        }

        uart2_task();                                /* USART2 task */
        uart1_task();                                /* USART1 task */
        if (wait_loop_exit != 0) {
            wait_loop_exit = 0;
            break;
        }
    }
    return 1;
}

/* 0x5d18  callers: 1
 * Take-and-clear the power-key event flag (1 = key was pressed and released). */
u8 key_take_power_event(void)
{
    u8 ev = key_power_event;

    if (ev != 0)
        key_power_event = 0;
    return ev;
}

/* 0x5d2c  callers: 1
 * Reset power-key event and latched-press state. */
void key_reset(void)
{
    key_power_event = 0;
    key_power_latched = 0;
}

/* 0x5d40  callers: 1
 * Power-key debounce, called once per main-loop pass.  Key = P1_16, active low.
 *   key_power_event  event out (1 for one pass, on release of a qualified press)
 *   key_power_latched  latched "pressed"
 *   key_power_count  pass counter while held
 *   key_power_state  state: 0 = counting, 1 = qualified (held > 4 passes), 2 = idle
 * A press that is released before it qualifies never clears key_power_latched, so the
 * counter simply keeps running on later passes (as coded). */
void key_power_debounce(void)
{
    u8 pressed = (gpio_rd(P(1,16)) == 0) ? 1 : 0;
    u8 old;

    key_power_event = 0;

    if (pressed != key_power_latched && pressed != 0) {
        key_power_latched = pressed;
        key_power_state = 0;
        key_power_count = 0;
    }
    if (key_power_latched == 0 && pressed == 0)
        key_power_state = 2;

    switch (key_power_state) {
    case 0:
        old = key_power_count;
        key_power_count = key_power_count + 1;
        if (old > 3)
            key_power_state = 1;
        break;
    case 1:
        if (pressed == 0) {
            key_power_event = key_power_latched;                      /* = 1: key event */
            key_power_latched = 0;
            key_power_state = 2;
        }
        break;
    case 2:
    default:
        break;
    }
}

/* 0x5de4  callers: 17
 * Program the LED pattern engine (all callers are in led_mode 0x3cf0).
 *   led_a, led_b   LED indexes for led_write (0..4, 5 = none)
 *   t1, t2         phase 1 / phase 2 durations in ct32b1_tick_count ticks
 *   alt            1 = the two LEDs alternate, else they blink together
 * If led_a == led_b and it is 0 or 1 the LED "breathes" via PWM instead
 * (see led_tick).  rt_nop is an empty stub (was probably IRQ lock/unlock). */
void led_set_pattern(u8 led_a, u8 led_b, u8 t1, u8 t2, /* stack */ u8 alt)
{
    rt_nop(0);
    led_all_off();
    led_set_pair(led_a, led_b);
    led_set_times(t1, t2);
    led_set_alt(alt);
    rt_nop(1);
}

/* 0x5e18  callers: 1
 * LED pattern tick, called from the timer IRQ (0x7e6a) with the current
 * phase time `t` (ct32b1_tick_count, which timer_ct32b1_count_reset resets).
 *   led_pwm_duty  last PWM duty written
 *   led_time1 / led_time2  phase 1 / phase 2 length
 *   led_a / led_b  LED a / LED b
 *   led_phase  phase (1 or 2; anything else = stopped)
 *   led_alternate  alternate flag
 * Breathing: duty runs 100 -> 0 over phase 1 and 0 -> 100 over phase 2 in
 * steps of 2, written with pwm_set(1, duty) (SCT PWM channel 1 ?). */
void led_tick(s32 t)
{
    u8 duty;
    u32 step;

    if ((led_a == 0 || led_a == 1) && led_a == led_b) {
        /* same LED twice, index 0 or 1: breathe */
        switch (led_phase) {
        case 1:
            if (led_time1 % 50 != 0) {
                duty = 50 - (t * 50) / led_time1;
            } else {
                step = led_time1 / 50;
                duty = 50 - t / step;
            }
            duty = (u8)(duty << 1);
            if (led_pwm_duty != duty) {
                led_pwm_duty = duty;
                pwm_set(1, duty);
            }
            if (t >= led_time1) {
                led_phase = 2;
                timer_ct32b1_count_reset();
            }
            break;
        case 2:
            if (led_time2 % 50 != 0) {
                duty = (t * 50) / led_time2;
            } else {
                step = led_time2 / 50;
                duty = t / step;
            }
            duty = (u8)(duty << 1);
            if (led_pwm_duty != duty) {
                led_pwm_duty = duty;
                pwm_set(1, duty);
            }
            if (t >= led_time2) {
                led_phase = 1;
                timer_ct32b1_count_reset();
            }
            break;
        default:
            break;
        }
    } else {
        /* blink / alternate */
        switch (led_phase) {
        case 1:
            if (t >= led_time1) {
                if (led_alternate == 1) {
                    led_write(led_a, 1);
                    led_write(led_b, 0);
                } else {
                    led_write(led_a, 0);
                    led_write(led_b, 0);
                }
                led_phase = 2;
                timer_ct32b1_count_reset();
            }
            break;
        case 2:
            if (t >= led_time2) {
                if (led_alternate == 1) {
                    led_write(led_a, 0);
                    led_write(led_b, 1);
                } else {
                    led_write(led_a, 1);
                    led_write(led_b, 1);
                }
                led_phase = 1;
                timer_ct32b1_count_reset();
            }
            break;
        default:
            break;
        }
    }
}

/* 0x5fd0  callers: 1
 * All six LED outputs low. */
void led_all_off(void)
{
    gpio_wr(P(2,10), 0);
    gpio_wr(P(2,9),  0);
    gpio_wr(P(2,2),  0);
    gpio_wr(P(2,8),  0);
    gpio_wr(P(2,6),  0);
    gpio_wr(P(2,18), 0);
}

/* 0x6014  callers: 1   set the two LED indexes of the pattern */
void led_set_pair(u8 led_a, u8 led_b)
{
    led_a = led_a;
    led_b = led_b;
}

/* 0x6028  callers: 1   set the two phase durations of the pattern */
void led_set_times(u8 t1, u8 t2)
{
    led_time1 = t1;
    led_time2 = t2;
}

/* 0x603c  callers: 1   set the alternate flag of the pattern */
void led_set_alt(u8 alt)
{
    led_alternate = alt;
}

/* 0x6048  callers: 8
 * Drive one LED by index.  rt_switch8 table at 0x6054: 06 04 09 0f 15 1b 21 22
 * (6 cases + default).  P2_18 is cleared by led_all_off but has no index here. */
void led_write(u8 led, u8 on)
{
    switch (led) {
    case 0:  gpio_wr(P(2,2),  on); break;
    case 1:  gpio_wr(P(2,8),  on); break;
    case 2:  gpio_wr(P(2,9),  on); break;
    case 3:  gpio_wr(P(2,10), on); break;
    case 4:  gpio_wr(P(2,6),  on); break;
    case 5:  break;                               /* "no LED" */
    default: break;
    }
}

/* 0x60a0  callers: 2
 * USART1 receive task (main loop, and the wait loop key_wait_power_loop).
 *   uart1_rx_buf_ptr  u8*  pointer to the USART1 rx buffer
 *   uart1_rx_count  u32  rx byte count (get_uart1_rx_count), uart1_rx_flag u8 rx flag (get_uart1_rx_flag)
 *   uart1_partial_pending  u8   1 = a partial frame (< 12 bytes) is pending, timer running
 *   uart_tx_buf       reply / work buffer passed to the frame parser
 * power_state_code == 3: everything received is handed to uart2_send (raw
 * forward / echo ?) and the buffer is flushed.
 * Otherwise a frame needs at least 12 bytes; a shorter one is dropped after
 * timer_soft4_get() reaches 30000 (30 s if that counter is in ms ?) or when the rx
 * flag uart1_rx_flag is set. */
void uart1_task(void)
{
    u32 out = 0;
    u8  n   = 0;

    if (power_get_state_code() == 3) {
        n = (u8)get_uart1_rx_count();
        if (n > 0) {
            uart2_send(uart1_rx_buf_ptr, n);
            uart1_rx_reset();                            /* flush rx */
        }
        return;
    }

    if (get_uart1_rx_flag() != 0 || (timer_soft4_get() >= 30000 && uart1_partial_pending == 1)) {
        uart1_rx_reset();                                /* flush rx */
        uart1_partial_pending = 0;
        return;
    }

    n = (u8)get_uart1_rx_count();
    if (n <= 0)
        return;

    if (n < 12) {
        if (uart1_partial_pending == 0) {
            timer_soft4_reset();                            /* restart the timeout counter */
            uart1_partial_pending = 1;
        }
        return;
    }

    cmd_frame_dispatch(n, uart1_rx_buf_ptr, uart_tx_buf, &out);             /* binary frame parser */
    uart1_rx_reset();                                    /* flush rx */
    /* note: uart1_partial_pending is not cleared on this path */
}


/* ======================================================================
 * Part 5
 * ====================================================================== */

/*
 * part_5.c -- LPC11U67 application, functions 0x613c .. 0x7177
 *
 * Hand-decompiled from app.txt.  Not meant to compile.
 *
 * Helpers referenced from outside this range (as read in app.txt):
 *   power_get_state_code()            returns power_state_code (0x100002e9)
 *   get_uart4_rx_count()            returns uart4_rx_count (u32 RX byte count of the "USART4" ring)
 *   fn_8280()            returns uart4_rx_flag (u8 RX error/overflow flag of same)
 *   uart4_rx_reset()            NVIC-disable IRQ 11, uart4_rx_count = 0, uart4_rx_flag = 0, NVIC-enable IRQ 11
 *   get_uart4_rx_buf()            returns (u8 *)0x100019c0  (RX buffer for the 0x6150 task)
 *   get_uart2_rx_count()/get_uart2_rx_flag()  return uart2_rx_count (u32 count) / uart2_rx_flag (u8 flag)
 *   uart2_rx_reset()            NVIC-disable IRQ 12, uart2_rx_count = 0, uart2_rx_flag = 0, NVIC-enable IRQ 12
 *   get_uart2_rx_buf()            returns (u8 *)0x10001940  (RX buffer for the 0x6b48 task)
 *   get_uart0_rx_buf()/get_uart1_rx_buf()  return (u8 *)0x100018c0 / (u8 *)0x10001900
 *   uart2_send(buf, n)      blocking TX of n bytes on the UART at 0x40070000 (via uart2_send_blocking)
 *   uart1_send(buf, n)      blocking TX of n bytes on the UART at 0x4006c000 (via uart1_send_blocking)
 *   timer_soft[]             u32 software timers, incremented elsewhere (1 ms tick ?):
 *                        timer_soft0_get16 -> (u16)timer_soft[0], timer_soft1_get -> [1], timer_soft2_get -> [2],
 *                        timer_soft3_get -> [3], timer_soft5_get -> [5];
 *                        timer_soft0_reset clears [0], timer_soft3_reset clears [3], timer_soft5_reset clears [5]
 *   rt_uidiv(num, den)    Keil unsigned divide, returns quotient
 *   uart4_beef_frame_handler(len, buf, flag)  "BE EF 10" frame checker; returns -2 when
 *                        len < buf[3] + 7 (frame not complete yet)
 *   cmd_frame_dispatch(len, buf, txbuf, &out)  binary frame handler wrapper (-> uart_beef_frame_handler)
 *   eeprom_addr_split(addr, &dev, buf, &len)  EEPROM address -> I2C device byte + address bytes
 *   i2c1_master_write_read(dev, wbuf, wlen, rbuf, rlen)  I2C0 write-then-read
 *   i2c1_master_write(dev, wbuf, wlen)              I2C0 write
 *   uart1_send_beef_status(cmd, arg)    builds and sends a 0xd1/0xd2/0xd5/0xda reply frame
 *   gpio_dir_clear(port, pin)   LPC_GPIO->DIR[port] &= ~(1 << pin)
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed int     s32;

#define P(port, pin)  (((port) << 8) | (pin))
#define REG32(a)      (*(volatile u32 *)(a))
#define REG16(a)      (*(volatile u16 *)(a))

/* 0x613c  callers: 2 */
/* Thin wrapper: pass (len, buf, flag) straight to the BE-EF frame checker. */
s32 cmd_beef_check_wrap(u32 len, u8 *buf, u32 flag)
{
    return uart4_beef_frame_handler(len, buf, flag);
}

/* 0x6150  callers: 1 */
/* Main-loop task for the UART whose RX count is uart4_rx_count (the doc calls it
 * USART4).  In power_state_code 3 it is a transparent bridge: everything
 * received is retransmitted with uart2_send (UART at 0x40070000).  Otherwise it
 * waits for a complete "BE EF 10 ..." frame (12-byte header, optional payload
 * of uart4_first_len bytes), validates it with cmd_beef_check_wrap and then just drops the RX
 * buffer -- the frames only update flags inside uart4_beef_frame_handler. */
void uart4_task(void)
{
    u8 n = 0;
    u8 n2 = 0;

    if (power_get_state_code() == 3) {
        n = (u8)get_uart4_rx_count();
        if (n > 0) {
            uart2_send(uart4_rx_buf_ptr, n);          /* uart4_rx_buf_ptr: u8 * RX buffer (0x100019c0) */
            uart4_rx_reset();
        }
        return;
    }

    /* RX error, or an incomplete frame has been sitting for >= 30000 ticks */
    if (fn_8280() != 0 || (timer_soft5_get() >= 30000 && uart4_partial_timing == 1)) {
        uart4_second_frame = 0;
        uart4_rx_reset();
        uart4_partial_timing = 0;
        return;
    }

    n = (u8)get_uart4_rx_count();
    if (n <= 0)
        return;

    if (n < 12) {
        if (uart4_partial_timing == 0) {
            uart4_second_frame = 0;
            timer_soft5_reset();                   /* restart timeout timer timer_soft[5] */
            uart4_partial_timing = 1;
        }
        return;
    }

    /* n >= 12 */
    if (uart4_second_frame == 0) {
        /* header phase */
        if (cmd_beef_check_wrap(n, uart4_rx_buf_ptr, uart4_frame_flag) == -2)
            return;

        if (uart4_rx_buf_ptr[3] == 6 && uart4_rx_buf_ptr[9] == 2) {
            uart4_frame_flag = 0;
            uart4_first_len = 13;
        } else if (uart4_rx_buf_ptr[3] == 5 && uart4_rx_buf_ptr[9] == 1) {
            if (uart4_rx_buf_ptr[11] == 7)
                uart4_frame_flag = 1;
            uart4_first_len = 12;
        }

        n2 = (u8)get_uart4_rx_count();
        if (n2 > n && uart4_rx_buf_ptr[11] == 7) {
            uart4_second_frame = 1;                  /* more bytes arrived: second frame follows */
            return;
        }
        uart4_rx_reset();
        return;
    }

    /* uart4_second_frame != 0: second frame, starting uart4_first_len bytes into the buffer */
    if (n < uart4_first_len + 12)
        return;
    if (cmd_beef_check_wrap(n - uart4_first_len, uart4_rx_buf_ptr + uart4_first_len, uart4_frame_flag) == -2)
        return;
    uart4_frame_flag = 0;
    uart4_second_frame = 0;
    uart4_rx_reset();
}

/* 0x62b0  callers: 1 (SysTick handler 0x7bd4) */
/* Tick-driven pulse generator on P1_3 / P1_4.  Emits pulse_count pulses; each
 * pulse drives the two pins to opposite levels (polarity from pulse_dir) for
 * pulse_on_ticks ticks, then both high for pulse_off_ticks ticks.
 *   pulse_state state: 0 idle, 1 start pulse, 2 pulse active, 3 end pulse, 4 gap
 *   pulse_ticks tick counter, pulse_done pulses done */
void gpio_pulse_tick(void)
{
    if (pulse_state == 0)
        return;

    pulse_ticks = pulse_ticks + 1;

    if (pulse_done >= pulse_count)
        return;

    if (pulse_state == 1) {
        if (pulse_dir == 0) {
            gpio_wr(P(1,3), 1);
            gpio_wr(P(1,4), 0);
        } else {
            gpio_wr(P(1,3), 0);
            gpio_wr(P(1,4), 1);
        }
        pulse_state = 2;
    } else if (pulse_state == 2) {
        if (pulse_ticks >= pulse_on_ticks) {
            pulse_state = 3;
            pulse_ticks = 0;
        }
    } else if (pulse_state == 3) {
        gpio_wr(P(1,3), 1);
        gpio_wr(P(1,4), 1);
        pulse_state = 4;
    } else if (pulse_state == 4) {
        if (pulse_ticks >= pulse_off_ticks) {
            pulse_done = pulse_done + 1;
            if (pulse_done >= pulse_count) {
                pulse_state = 0;
                pulse_ticks = 0;
            } else {
                pulse_state = 1;
                pulse_ticks = 0;
            }
        }
    }
}

/* 0x63b8  callers: 2 */
/* NVIC_DisableIRQ (inlined CMSIS copy #1). */
void nvic_disable_irq_a(u32 irq)
{
    REG32(0xe000e180) = 1u << (irq & 0x1f);      /* NVIC->ICER[0] */
}

/* 0x63cc  callers: 9 */
/* NVIC_DisableIRQ (copy #2, used by the UART modules). */
void nvic_disable_irq(u32 irq)
{
    REG32(0xe000e180) = 1u << (irq & 0x1f);      /* NVIC->ICER[0] */
}

/* 0x63e0  callers: 8 */
/* NVIC_EnableIRQ (copy #1). */
void nvic_enable_irq_a(u32 irq)
{
    REG32(0xe000e100) = 1u << (irq & 0x1f);      /* NVIC->ISER[0] */
}

/* 0x63f4  callers: 4 */
/* NVIC_EnableIRQ (copy #2). */
void nvic_enable_irq_b(u32 irq)
{
    REG32(0xe000e100) = 1u << (irq & 0x1f);      /* NVIC->ISER[0] */
}

/* 0x6408  callers: 2 */
/* NVIC_EnableIRQ (copy #3, timers). */
void nvic_enable_irq_c(u32 irq)
{
    REG32(0xe000e100) = 1u << (irq & 0x1f);      /* NVIC->ISER[0] */
}

/* 0x641c  callers: 9 */
/* NVIC_EnableIRQ (copy #4, UART modules). */
void nvic_enable_irq(u32 irq)
{
    REG32(0xe000e100) = 1u << (irq & 0x1f);      /* NVIC->ISER[0] */
}

/* 0x6430  callers: 1 */
/* NVIC_SetPriority (CMSIS Cortex-M0 version). */
void nvic_set_priority(s32 irq, u32 prio)
{
    u32 shift = (irq & 3) * 8;

    if (irq < 0) {
        /* SCB->SHP[] : 0xe000ed1c + (((irq & 0xf) - 8) >> 2) * 4 */
        volatile u32 *shp = (volatile u32 *)(0xe000ed1c + ((((u32)irq & 0xf) - 8) >> 2) * 4);
        *shp = (*shp & ~(0xffu << shift)) | (((prio << 6) & 0xff) << shift);
    } else {
        /* NVIC->IP[] : 0xe000e400 + (irq >> 2) * 4 */
        volatile u32 *ip = (volatile u32 *)(0xe000e400 + ((u32)irq >> 2) * 4);
        *ip = (*ip & ~(0xffu << shift)) | (((prio << 6) & 0xff) << shift);
    }
}

/* 0x64a8  callers: 1 (power_off, 0x743c) */
/* If eeprom_probed is clear, switch the EEPROM driver to I2C device 0xa4,
 * geometry-table index 5. */
void eeprom_select_a4(void)
{
    if (eeprom_probed == 0) {
        eeprom_i2c_addr = 0xa4;
        eeprom_set_type(5);
    }
}

/* 0x64c8  callers: 7 */
/* Read len bytes from EEPROM address addr into dst, one byte at a time.
 * Returns the AND of all per-byte results (1 = every read succeeded). */
u32 eeprom_read(u16 addr, u8 *dst, u16 len)
{
    u32 ok = 1;
    u16 i;

    for (i = 0; i < len; i++)
        ok &= eeprom_read_byte((u16)(addr + i), dst + i);
    return ok;
}

/* 0x64f4  callers: 5 */
/* Read one EEPROM byte: eeprom_addr_split splits addr into device byte + address
 * bytes, i2c1_master_write_read does the I2C write-address / read-1-byte transfer. */
u32 eeprom_read_byte(u16 addr, u8 *dst)
{
    u8  dev;          /* [sp+4]  */
    u8  alen;         /* [sp+8]  */
    u8  abuf[4];      /* [sp+0xc] */
    u32 r = 0;

    eeprom_addr_split(addr, &dev, abuf, &alen);
    r = i2c1_master_write_read(dev, abuf, alen, dst, 1);
    return r;
}

/* 0x6524  callers: 1 */
/* Set the EEPROM geometry-table index. */
void eeprom_set_type(u8 idx)
{
    eeprom_type_idx = idx;
}

/* 0x6530  callers: 1 */
/* Write len bytes from src to EEPROM address addr, one byte at a time.
 * Returns the AND of all per-byte results. */
u32 eeprom_write(u16 addr, u8 *src, u16 len)
{
    u32 ok = 1;
    u16 i;

    for (i = 0; i < len; i++)
        ok &= eeprom_write_byte((u16)(addr + i), src[i]);
    return ok;
}

/* 0x655c  callers: 4 */
/* Write one EEPROM byte: address bytes from eeprom_addr_split, data byte appended,
 * sent as one I2C write.  No write-cycle delay here. */
u32 eeprom_write_byte(u16 addr, u8 val)
{
    u8  dev;          /* [sp+0] */
    u8  alen;         /* [sp+4] */
    u8  buf[4];       /* [sp+8] */
    u32 r = 0;

    eeprom_addr_split(addr, &dev, buf, &alen);
    buf[alen] = val;
    r = i2c1_master_write(dev, buf, (u8)(alen + 1));
    return r;
}

/* 0x6590  callers: 2 (binary frame handler 0x3c20) */
/* Act on a one-byte command code and send the matching reply frame. */
void cmd_exec_code(u32 cmd)
{
    switch (cmd) {
    case 0x08:
        wait_loop_exit = 1;                      /* power-off request ? */
        uart1_send_beef_status(0xd5, 0x55);
        break;
    case 0xd2:
        uart1_send_beef_status(0xd2, 0);
        break;
    case 0x4e:
        uart_wake_token = 0;             /* -> wake reason 4 in main */
        uart1_send_beef_status(0xd5, 0x55);
        break;
    case 0xd1:
        uart1_send_beef_status(0xd1, 0);
        break;
    default:
        break;
    }
}

/* 0x65e8  callers: 1 */
/* Getter for phy_wol_state. */
u8 phy_wol_state_get(void)
{
    return phy_wol_state;
}

/* 0x65f4  callers: 1 */
/* Leave WoL mode: pulse P1_5 low for 1000 ms. */
void phy_leave_wol(void)
{
    gpio_wr(P(1,5), 0);
    delay_ticks(1000);
    gpio_wr(P(1,5), 1);
}

/* 0x6614  callers: 2 */
/* Setter for phy_wol_state. */
void phy_wol_state_set(u8 v)
{
    phy_wol_state = v;
}

/* 0x6620  callers: 1 */
/* Program the PHY (MDIO address 3) for magic-packet wake-up, then set
 * phy_wol_state = 2.  Register 0x1f is the page select. */
void phy_arm_wol(void)
{
    u16 mac[3];       /* 6 bytes at [sp], read back as three LE u16 */
    u16 r1 = 0;
    u16 r0 = 0;
    u16 t  = 0;

    setting_get_mac((u8 *)mac);                  /* copy settings.mac[0..5] */

    /* wait (max 200 x 200 ms) for reg0 bit15 (reset) and reg1 bit4 to clear */
    while (t < 200) {
        r0 = (u16)mdio_read(3, 0);
        r1 = (u16)mdio_read(3, 1);
        if ((r0 & 0x8000) == 0 && (r1 & 0x10) == 0)
            break;
        t++;
        delay_ticks(200);
    }

    mdio_write(3, 0x1f, 0x12);           /* page 0x12: MAC address */
    mdio_write(3, 0x10, mac[0]);
    mdio_write(3, 0x11, mac[1]);
    mdio_write(3, 0x12, mac[2]);

    mdio_write(3, 0x1f, 0x11);           /* page 0x11: WoL control */
    mdio_write(3, 0x11, 0x1fff);
    mdio_write(3, 0x10, 0x1000);

    mdio_write(3, 0x1f, 7);              /* page 7 */
    t = (u16)mdio_read(3, 0x14);
    t |= 0x8000;
    mdio_write(3, 0x14, t);

    mdio_write(3, 0x1f, 0x11);           /* page 0x11 */
    t = (u16)mdio_read(3, 0x13);
    t |= 0x8000;
    mdio_write(3, 0x13, t);

    mdio_write(3, 0x1f, 7);              /* page 7 */
    t = (u16)mdio_read(3, 0x13);
    t |= 0x0400;
    mdio_write(3, 0x13, t);

    phy_wol_state = 2;
}

/* 0x6734  callers: 1 (main, 0x8ef6) */
/* While WoL is armed (state 2), track P1_28 in phy_pme_last.  On a change to low
 * set phy_wol_state = 1 and return 1; otherwise return 0. */
u32 phy_pme_fell(void)
{
    if (phy_wol_state == 2) {
        if (gpio_rd(P(1,28)) != phy_pme_last) {
            phy_pme_last = (gpio_rd(P(1,28)) == 1) ? 1 : 0;
            if (phy_pme_last == 0) {
                phy_wol_state = 1;
                return 1;
            }
        }
    }
    return 0;
}

/* 0x6780  callers: 0 (vector IRQ0, PIN_INT0) */
/* PINT0: if pending and edge-sensitive, clear the rise (or else fall)
 * detect latch and the status bit.  No other action. */
void pint0_irq(void)
{
    if ((LPC_PINT->IST & 1) && !(LPC_PINT->ISEL & 1)) {
        if (LPC_PINT->RISE & 1)
            LPC_PINT->RISE = 1;
        else if (LPC_PINT->FALL & 1)
            LPC_PINT->FALL = 1;
        LPC_PINT->IST = 1;
    }
}

/* 0x67cc  callers: 0 (vector IRQ1, PIN_INT1) */
/* PINT1: same acknowledge-only handler, mask 0x02. */
void pint1_irq(void)
{
    if ((LPC_PINT->IST & 2) && !(LPC_PINT->ISEL & 2)) {
        if (LPC_PINT->RISE & 2)
            LPC_PINT->RISE = 2;
        else if (LPC_PINT->FALL & 2)
            LPC_PINT->FALL = 2;
        LPC_PINT->IST = 2;
    }
}

/* 0x6814  callers: 0 (vector IRQ2, PIN_INT2) */
/* PINT2: empty handler. */
void pint2_irq(void)
{
}

/* 0x6816  callers: 0 (vector IRQ3, PIN_INT3) */
/* PINT3: the only pin interrupt that does work.  If pending: disable the
 * falling-edge enable, run ir_on_edge(0) (pulse-width measurement, uses timer
 * timer_timeout_get -- IR / remote input ?), acknowledge, re-enable falling edge. */
void pint3_irq(void)
{
    if (pint_pending(3)) {                    /* LPC_PINT->IST & (1 << 3) */
        pint_disable(3, 0);                   /* edge mode: CIENF |= 1 << 3 */
        ir_on_edge(0);
        pint_ack(3);                      /* clear RISE/FALL latch + IST */
        pint_enable(3, 0);                   /* edge mode: SIENF |= 1 << 3 */
    }
}

/* 0x6840  callers: 0 (vector IRQ4, PIN_INT4) */
/* PINT4: acknowledge-only handler, mask 0x10. */
void pint4_irq(void)
{
    if ((LPC_PINT->IST & 0x10) && !(LPC_PINT->ISEL & 0x10)) {
        if (LPC_PINT->RISE & 0x10)
            LPC_PINT->RISE = 0x10;
        else if (LPC_PINT->FALL & 0x10)
            LPC_PINT->FALL = 0x10;
        LPC_PINT->IST = 0x10;
    }
}

/* 0x6888  callers: 0 (vector IRQ5, PIN_INT5) */
/* PINT5: acknowledge-only handler, mask 0x20. */
void pint5_irq(void)
{
    if ((LPC_PINT->IST & 0x20) && !(LPC_PINT->ISEL & 0x20)) {
        if (LPC_PINT->RISE & 0x20)
            LPC_PINT->RISE = 0x20;
        else if (LPC_PINT->FALL & 0x20)
            LPC_PINT->FALL = 0x20;
        LPC_PINT->IST = 0x20;
    }
}

/* 0x68d0  callers: 0 (vector IRQ6, PIN_INT6) */
/* PINT6: acknowledge-only handler, mask 0x40. */
void pint6_irq(void)
{
    if ((LPC_PINT->IST & 0x40) && !(LPC_PINT->ISEL & 0x40)) {
        if (LPC_PINT->RISE & 0x40)
            LPC_PINT->RISE = 0x40;
        else if (LPC_PINT->FALL & 0x40)
            LPC_PINT->FALL = 0x40;
        LPC_PINT->IST = 0x40;
    }
}

/* 0x6918  callers: 0 (vector IRQ7, PIN_INT7) */
/* PINT7: acknowledge-only handler, mask 0x80. */
void pint7_irq(void)
{
    if ((LPC_PINT->IST & 0x80) && !(LPC_PINT->ISEL & 0x80)) {
        if (LPC_PINT->RISE & 0x80)
            LPC_PINT->RISE = 0x80;
        else if (LPC_PINT->FALL & 0x80)
            LPC_PINT->FALL = 0x80;
        LPC_PINT->IST = 0x80;
    }
}

/* 0x6960  callers: 2 (power_off 0x74b4, and 0x855e) */
/* Standby pin teardown: return several peripheral pins to plain GPIO
 * (IOCON function arg as passed), make P2_11 / P2_12 inputs, and drive a
 * handful of main-side outputs to their off levels. */
void power_standby_pins(void)
{
    iocon_cfg(2, 5, 1);
    iocon_cfg(2, 14, 1);
    iocon_cfg(0, 12, 1);
    iocon_cfg(1, 27, 0);
    iocon_cfg(0, 20, 1);

    gpio_as_input(P(2,11), 0);
    gpio_as_input(P(2,12), 0);
    gpio_dir_clear(2, 11);                      /* DIR[2] &= ~(1 << 11) */
    gpio_dir_clear(2, 12);                      /* DIR[2] &= ~(1 << 12) */
    iocon_cfg(2, 11, 1);
    iocon_cfg(2, 12, 1);

    gpio_wr(P(1,31), 0);
    gpio_wr(P(0,6), 0);
    gpio_sel_p1_23_22(0);                          /* 2-bit selector P1_23/P1_22 := 0 */
    gpio_wr(P(1,20), 1);
    gpio_wr(P(0,8), 0);
    gpio_wr(P(1,29), 0);
}

/* 0x6a04  callers: 5 */
/* Send the reply to an ASCII '#' command via uart2_send, built in uart_tx_buf[]:
 *   has_val == 0:            "P\r" if ok == 1 else "F\r"
 *   has_val != 0, ok == 1:   'P', val, '\r'
 *   has_val != 0, ok != 1:   "F\r" */
void cmd_ascii_reply(u8 ok, u8 has_val, u8 val)
{
    if (has_val == 0) {
        if (ok == 1)
            uart_tx_buf[0] = 'P';
        else
            uart_tx_buf[0] = 'F';
        uart_tx_buf[1] = '\r';
        uart2_send(uart_tx_buf, 2);
    } else if (ok == 1) {
        uart_tx_buf[0] = 'P';
        uart_tx_buf[1] = val;
        uart_tx_buf[2] = '\r';
        uart2_send(uart_tx_buf, 3);
    } else {
        uart_tx_buf[0] = 'F';
        uart_tx_buf[1] = '\r';
        uart2_send(uart_tx_buf, 2);
    }
}

/* 0x6a68  callers: 1 (binary frame handler, 0x3cd2) */
/* Two/three-byte binary acknowledge for a command code, sent via uart2_send.
 *   1    : request power-on (uart_wake_token = 0), reply 00 01
 *   0x6d : wait_loop_exit = 1,                              reply 00 6d
 *   0xff :                                          reply 00 ff 01
 *   else :                                          reply 01 <cmd>  */
void cmd_binary_ack(u32 cmd)
{
    u8 n = 0;

    switch (cmd) {
    case 0x01:
        uart_wake_token = 0;
        uart_tx_buf[0] = 0;
        uart_tx_buf[1] = (u8)cmd;
        n = 2;
        break;
    case 0x6d:
        wait_loop_exit = 1;
        uart_tx_buf[0] = 0;
        uart_tx_buf[1] = (u8)cmd;
        n = 2;
        break;
    case 0xff:
        uart_tx_buf[0] = 0;
        uart_tx_buf[1] = (u8)cmd;
        uart_tx_buf[2] = 1;
        n = 3;
        break;
    default:
        uart_tx_buf[0] = 1;
        uart_tx_buf[1] = (u8)cmd;
        n = 2;
        break;
    }
    uart2_send(uart_tx_buf, n);
}

/* 0x6adc  callers: 1 (main, 0x8eb8) */
/* Take the UART wake token: returns its current value (0 = wake requested)
 * and re-arms it to 0x14. */
u8 uart_wake_token_take(void)
{
    u8 v = uart_wake_token;

    if (v != 0x14)
        uart_wake_token = 0x14;
    return v;
}

/* 0x6af0  callers: 1 (power_off, 0x74ac) */
/* Cache the four UART RX-buffer pointers and reset the UART task state. */
void uart_tasks_init(void)
{
    uart0_rx_buf_ptr = get_uart0_rx_buf();                  /* 0x100018c0 */
    uart1_rx_buf_ptr = get_uart1_rx_buf();                  /* 0x10001900 */
    uart2_rx_buf_ptr = get_uart2_rx_buf();                  /* 0x10001940 */
    uart4_rx_buf_ptr = get_uart4_rx_buf();                  /* 0x100019c0 */
    uart_wake_token = 0x14;
    uart0_dev_count = 0;
    uart0_dev_sel = 0;
    uart0_link_flag = 0;
}

/* 0x6b48  callers: 2 (0x5cec, main 0x8912) */
/* Main-loop task for the UART whose RX count is uart2_rx_count (the doc calls it
 * USART2).  power_state_code 3: bridge RX to uart1_send (UART at 0x4006c000).
 * Otherwise: a line starting '#' and ending '\r' is an ASCII command,
 * compared (8 bytes) against the three strings pointed to by cmd_ascii_table[]
 * ("#00500 0", "#00500 1", "#00501 1" at rom_9958/9964/9970); anything
 * else of >= 12 bytes goes to the binary frame handler cmd_frame_dispatch. */
void uart2_task(void)
{
    u32 val;          /* [sp+0] */
    u32 out = 0;      /* [sp+4] */
    u8  n = 0;
    u8  i = 0;
    u8  c;

    if (power_get_state_code() == 3) {
        n = (u8)get_uart2_rx_count();
        if (n > 0) {
            uart1_send(uart2_rx_buf_ptr, n);          /* uart2_rx_buf_ptr: u8 * RX buffer (0x10001940) */
            uart2_rx_reset();
        }
        return;
    }

    /* RX error, or a partial binary frame older than 30000 ticks: flush */
    if (get_uart2_rx_flag() != 0 || (timer_soft3_get() >= 30000 && uart2_partial_timing == 1)) {
        uart2_rx_reset();
        uart2_partial_timing = 0;
        return;
    }

    n = (u8)get_uart2_rx_count();
    if (n <= 0)
        return;

    if (n == 1) {
        if (uart2_rx_buf_ptr[0] == '#') {
            uart2_ascii_line = 1;                  /* ASCII line in progress */
        } else if (uart2_rx_buf_ptr[0] == '\r') {
            uart2_rx_reset();
            uart2_ascii_line = 0;
        } else {
            uart2_ascii_line = 0;
        }
    }

    if (uart2_ascii_line == 1) {
        if (n == 9) {
            if (uart2_rx_buf_ptr[n - 1] != '\r')
                return;
            for (i = 0; i < 3; i++) {
                if (rt_memcmp(uart2_rx_buf_ptr, cmd_ascii_table[i], 8) == 0)
                    break;
            }
            if (i == 0) {                /* "#00500 0": WoL enable := 0 */
                val = 0;
                setting_set_wol_enable((u8 *)&val);     /* settings.wol_enable = val; write to EEPROM */
                cmd_ascii_reply(1, 0, 0);
            } else if (i == 1) {         /* "#00500 1": WoL enable := 1 */
                val = 1;
                setting_set_wol_enable((u8 *)&val);
                cmd_ascii_reply(1, 0, 0);
            } else if (i == 2) {         /* "#00501 1": query WoL enable */
                setting_get_wol_enable((u8 *)&val);     /* *val = settings.wol_enable */
                if ((u8)val == 0)
                    c = '0';
                else
                    c = '1';
                cmd_ascii_reply(1, 1, c);
            } else {
                cmd_ascii_reply(0, 0, 0);
            }
            uart2_rx_reset();
            uart2_ascii_line = 0;
        } else if (n > 1) {
            if (uart2_rx_buf_ptr[n - 1] != '\r')
                return;
            cmd_ascii_reply(0, 0, 0);            /* wrong length line: "F\r" */
            uart2_rx_reset();
            uart2_ascii_line = 0;
        }
        return;
    }

    /* binary traffic */
    if (n < 12) {
        if (uart2_partial_timing == 0) {
            timer_soft3_reset();                   /* restart timeout timer timer_soft[3] */
            uart2_partial_timing = 1;
        }
        return;
    }
    cmd_frame_dispatch(n, uart2_rx_buf_ptr, uart_tx_buf, &out);
    uart2_rx_reset();
}

/* 0x6cf4  callers: 0 (vector IRQ 13, SCT0_1) */
/* SCT0 interrupt.  Reads EVFLAG and, for the exact values 1, 2 or 3,
 * writes it back OR'ed with that value (write-1-to-clear).  Nothing else. */
void sct0_irq(void)
{
    switch (LPC_SCT0->EVFLAG) {                  /* 0x5000c0f4 */
    case 1:
        LPC_SCT0->EVFLAG |= 1;
        break;
    case 2:
        LPC_SCT0->EVFLAG |= 2;
        break;
    case 3:
        LPC_SCT0->EVFLAG |= 3;
        break;
    default:
        break;
    }
}

/* 0x6d3c  callers: 2 (0x2416, power_off 0x7510) */
/* SCT0 PWM time-base setup.  Period count pwm_period = 48000000 / (freq * 5);
 * unified 32-bit counter, event 0 = match 0 (the period). */
void pwm_init(u32 freq)
{
    pwm_period = rt_uidiv(48000000, freq * 5);

    LPC_SYSCON->SYSAHBCLKCTRL |= 0x80000000;     /* 0x40048080, bit 31 = SCT0/1 clock */
    LPC_SYSCON->PRESETCTRL    |= 0xfffffc00;     /* 0x40048004; value is (s32)0x80000000 >> 21 as coded */

    LPC_SCT0->CTRL   |= 0x00040004;              /* HALT_L | HALT_H */
    LPC_SCT0->CTRL   |= 0x00080008;              /* CLRCTR_L | CLRCTR_H */
    LPC_SCT0->CONFIG  = 0x00020001;              /* UNIFY | AUTOLIMIT_L */
    REG32(0x5000c054) = REG32(0x5000c054) & ~0xffu;  /* OUTPUTDIRCTRL low byte = 0 */
    REG16(0x5000c04c) = 0;                       /* REGMODE_L = 0: all match */
    REG32(0x5000c100) = 0;                       /* MATCH[0] */
    REG32(0x5000c100) = pwm_period;                  /* MATCH[0] */
    REG32(0x5000c200) = pwm_period;                  /* MATCHREL[0] */
    REG32(0x5000c304) |= 0x1000;                 /* EV0_CTRL: COMBMODE = match, MATCHSEL 0 */
    REG32(0x5000c300) |= 1;                      /* EV0_STATE: active in state 0 */
}

/* 0x6e00  callers: 4 (0x241e, 0x244a, 0x5e98, 0x5efa) */
/* Enable PWM output `ch` (0..3) at `duty` percent and start the SCT.
 * Note MATCHREL[1] / event 1 are shared, so every enabled channel gets the
 * most recently written duty. Output n: set on event 0, cleared on event 1. */
void pwm_set(u32 ch, u32 duty)
{
    u32 m = 0;

    m = rt_uidiv(pwm_period * duty, 100);

    LPC_SCT0->CTRL |= 0x00040004;                /* halt */
    LPC_SCT0->CTRL |= 0x00080008;                /* clear counter */

    switch (ch) {
    case 0:                                      /* IOCON PIO1_19 := func 2 */
        REG32(0x400440ac) = REG32(0x400440ac) & ~7u;
        REG32(0x400440ac) |= 2;
        break;
    case 1:                                      /* IOCON PIO2_2 := func 3 */
        REG32(0x400440fc) = REG32(0x400440fc) & ~7u;
        REG32(0x400440fc) |= 3;
        break;
    case 2:                                      /* IOCON PIO2_7 := func 2 */
        REG32(0x40044110) = REG32(0x40044110) & ~7u;
        REG32(0x40044110) |= 2;
        break;
    case 3:                                      /* IOCON PIO1_13 := func 2 */
        REG32(0x40044094) = REG32(0x40044094) & ~7u;
        REG32(0x40044094) |= 2;
        break;
    default:
        break;
    }

    REG32(0x5000c204)  = m;                      /* MATCHREL[1] */
    REG32(0x5000c30c) |= 0x1001;                 /* EV1_CTRL: MATCHSEL 1, COMBMODE = match */
    REG32(0x5000c308) |= 1;                      /* EV1_STATE */

    switch (ch) {
    case 0:
        REG32(0x5000c500) |= 1;                  /* OUT0_SET |= event 0 */
        REG32(0x5000c504) |= 2;                  /* OUT0_CLR |= event 1 */
        break;
    case 1:
        REG32(0x5000c508) |= 1;                  /* OUT1_SET */
        REG32(0x5000c50c) |= 2;                  /* OUT1_CLR */
        break;
    case 2:
        REG32(0x5000c510) |= 1;                  /* OUT2_SET */
        REG32(0x5000c514) |= 2;                  /* OUT2_CLR */
        break;
    case 3:
        REG32(0x5000c518) |= 1;                  /* OUT3_SET */
        REG32(0x5000c51c) |= 2;                  /* OUT3_CLR */
        break;
    default:
        break;
    }

    LPC_SCT0->CTRL &= ~0x00040004u;              /* un-halt: run */
}

/* 0x6fc0  callers: 2 (main 0x8944, 0x8f3e) */
/* Power-on-state housekeeping.  First call 30000 ticks (timer timer_soft[1])
 * after power-on: set the P0_20-dependent outputs, raise P1_24 and bring up
 * the fifth UART at 19200.  Afterwards: keep P1_8 / P0_22 following P0_20. */
void power_on_late_task(void)
{
    if (power_on_late_done == 0) {
        if (timer_soft1_get() < 30000)
            return;
        if (gpio_rd(P(0,20)) == 0) {
            gpio_wr(P(1,8), 0);
            gpio_wr(P(0,22), 1);
        }
        gpio_wr(P(1,24), 1);
        power_on_late_done = 1;
        uart4_init(19200);
    } else {
        if (gpio_rd(P(0,20)) == 0) {
            gpio_wr(P(1,8), 0);
            gpio_wr(P(0,22), 1);
        } else {
            if (cmd_p1_8_asserted == 0)
                gpio_wr(P(1,8), 1);
            gpio_wr(P(0,22), 0);
        }
    }
}

/* 0x7050  callers: 1 (main, 0x8e6a) */
/* Standby settle: once, 5000 ticks (timer timer_soft[2]) after entering standby,
 * reload settings, drop P0_7 if eco standby is selected, and set selector
 * gpio_sel_p1_13_12 to 0.  Returns standby_settled_flag. */
u8 standby_settled(void)
{
    if (standby_settled_flag == 0) {
        if (timer_soft2_get() >= 5000) {
            load_settings();
            if (setting_keep_rails() == 0)
                gpio_wr(P(0,7), 0);
            standby_settled_flag = 1;
            gpio_sel_p1_13_12(0);
        }
    }
    return standby_settled_flag;
}

/* 0x7094  callers: 1 (main, 0x8f3a) */
/* On-state fault monitor.  Starts 15000 ticks after power-on.  P1_27 high =
 * healthy (timer timer_soft[0] is kept at zero).  P1_27 low continuously for
 * 35000 ticks: wait 10 s, record fault code 2, kill outputs, LED mode 9,
 * wait 35 s, and if key_wait_power_loop() allows, power_off(2). */
void power_fault_monitor(void)
{
    if (power_fault_armed == 0) {
        if (timer_soft0_get16() >= 15000) {        /* (u16)timer_soft[0], signed compare */
            timer_soft0_reset();                   /* timer_soft[0] = 0 */
            power_fault_armed = 1;
        }
        return;
    }

    if (gpio_rd(P(1,27)) != 0) {
        timer_soft0_reset();
        return;
    }

    if (timer_soft0_get16() >= 35000) {
        delay_ticks(10000);
        power_fault_code = 2;
        gpio_wr(P(1,8), 0);
        led_mode(9);
        eeprom_flag_bit_write(4, 0);                   /* clear bit 4 of EEPROM byte 0x3e00 */
        gpio_wr(P(1,19), 0);
        gpio_wr(P(1,25), 0);
        gpio_sel_p2_21_20_n(1);
        power_clear_on_flags();                       /* power_is_on = 0, ... */
        delay_ticks(35000);
        if (key_wait_power_loop() != 0) {
            led_fault_latch = 0;
            power_off(2);
        }
    }
}

/* 0x7148  callers: 1 (0x25b4) */
/* Read the EEPROM byte pair at 0x3e03 / 0x3e04 (value and its complement).
 * Returns the value if the two sum to 0xff, else 0. */
u8 eeprom_read_checked_3e03(void)
{
    u8 inv;           /* [sp+0] */
    u8 val;           /* [sp+4] */

    eeprom_read_byte(0x3e03, &val);
    eeprom_read_byte(0x3e04, &inv);
    if (val + inv == 0xff)
        return val;
    return 0;
}


/* ======================================================================
 * Part 6
 * ====================================================================== */

/*
 * part_6.c -- LPC11U67 application, 0x7178 .. 0x826b
 *
 * Contents: setting / flag getters, power_on, power_off, 2-bit GPIO selector
 * helpers, three outgoing-frame builders, SysTick + CT16B1/CT32B0/CT32B1
 * interrupt handlers, software-timer getters/resets, timer setup, delay_ticks,
 * and the UART0/1/2/4 receive-buffer accessors and transmit wrappers.
 *
 * Conventions: P(port,pin) = (port<<8)|pin.  gpio_wr(id,val), gpio_rd(id),
 * gpio_dir(id,is_output), gpio_as_input(id,x), iocon_cfg(port,pin,func).
 * settings.* are the EEPROM-loaded bytes at 0x10001a44.. (see SEEDS.txt).
 *
 * SysTick CSR bracket: many functions here do
 *     SysTick->CTRL = SysTick->CTRL;        (0xE000E010, read then write back)
 * before and after touching a tick-updated variable.  It is rendered below
 * as SYSTICK_CSR_TOUCH().  It does NOT mask the interrupt (the value written
 * is the value read); its only side effect is clearing COUNTFLAG.  Probably
 * an empty/degenerate "critical section" macro in the source. ?
 *
 * Tick-rate caveat: timer_systick_init programs SysTick LOAD = 4800-1 with CLKSOURCE =
 * core clock.  uart0_init divides 48000000 (0x02dc6c00) for its baud rate,
 * so if the core really runs at 48 MHz the SysTick period is 100 us, not
 * 1 ms, and every "delay_ticks(n)" is n * 0.1 ms.  The tick is 1 ms only if the
 * core clock is 4.8 MHz.  Not resolvable from this range; numbers below are
 * given in ticks, exactly as in the code.
 */

#define SYSTICK_CSR_TOUCH()   (REG32(0xE000E010) = REG32(0xE000E010))

/* ROM templates used by the frame builders (flash, read-only):
 *   rom_9688[16] = BE EF 10 06 00 00 00 33 33 02 00 01 FF 00 00 00   (uart1_send_beef_status)
 *   rom_9698[16] = BE EF 10 06 00 00 00 33 33 02 00 01 FF 00 00 00   (uart4_send_beef_frame)
 *   rom_96a8[12] = 2A 20 01 01 01 03 50 00 00 00 00 00               (uart1_send_2a20_frame)
 */

/* 0x7178  callers: 1 */
/* true if settings.s2 (0x10001a4b) == 0.  setting_get_s2 copies that byte out. */
u8 setting_s2_is_zero(void)
{
    u8 v;
    setting_get_s2(&v);                    /* v = settings.s2 */
    return (v == 0) ? 1 : 0;
}

/* 0x7198  callers: 1 */
u8 power_get_flag_02f7(void)
{
    return g_02f7;
}

/* 0x71a4  callers: 3 */
/* true if settings.standby_mode == 0 ("keep standby rails"; non-zero = eco). */
u8 setting_keep_rails(void)
{
    u8 v;
    setting_get_standby_mode(&v);                    /* v = settings.standby_mode */
    return (v == 0) ? 1 : 0;
}

/* 0x71c4  callers: 2 */
u8 power_get_is_on(void)
{
    return power_is_on;
}

/* 0x71d0  callers: 1 */
/* true if settings.s4 (0x10001a4d) == 0.  Used by led_mode. */
u8 setting_s4_is_zero(void)
{
    u8 v;
    setting_get_s4(&v);                    /* v = settings.s4 */
    return (v == 0) ? 1 : 0;
}

/* 0x71f0  callers: 1 */
u8 power_get_wake_reason(void)
{
    return wake_reason;
}

/* 0x71fc  callers: 2 */
u8 power_get_flag_02f1(void)
{
    return g_02f1;
}

/* 0x7208  callers: 1 */
u8 power_get_flag_02f2(void)
{
    return g_02f2;
}

/* 0x7214  callers: 5 */
/* true if settings.wol_enable == 1. */
u8 setting_wol_enabled(void)
{
    u8 v;
    setting_get_wol_enable(&v);                    /* v = settings.wol_enable */
    return (v == 1) ? 1 : 0;
}

/* 0x7234  callers: 1 */
/* returns the mode argument of the last power_off() call */
u8 power_get_last_off_mode(void)
{
    return power_last_off_mode;
}

/* 0x7240  callers: 7 */
u8 power_get_state_code(void)
{
    return power_state_code;
}

/* 0x724c  callers: 9 */
/* Clears the "powered on" flag and some companion state, without touching
 * any rail.  Callers follow it with power_off()/power_on() sequences. */
void power_clear_on_flags(void)
{
    power_is_on = 0;
    power_set_flag_02e6(0);                     /* g_02e6 = 0 */
    power_set_flag_02e5(0);                     /* g_02e5 = 0 */
    factory_test10_active = 0;
    factory_test3_active = 0;
}

/* 0x7278  callers: 8 */
/* Power-on: clocks IOCON, brings up UART0/1/3, muxes four pins, runs the
 * rail sequencer, then (unless a non-0/non-6 state code aborts it) finishes
 * with P1_29 mirror, gpio_sel_p1_13_12(3) and P0_6 = 1. */
void power_on(void)
{
    LPC_SYSCON->SYSAHBCLKCTRL |= (1u << 16);        /* IOCON clock */

    uart0_init(19200);
    uart1_init(19200);
    uart3_init(19200);

    iocon_cfg(2, 5, 2);
    iocon_cfg(2, 14, 2);
    iocon_cfg(0, 12, 2);
    iocon_cfg(0, 20, 2);

    power_on_rails();

    power_is_on = 1;
    timer_soft0_reset();                                      /* timer_soft[0] = 0 */

    if (power_state_code != 0) {
        if (power_state_code != 6)
            return;                                 /* 0x7314 */

        /* power_state_code == 6 */
        if (gpio_rd(P(0,20)) == 0) {
            gpio_wr(P(1,8), 0);
            gpio_wr(P(0,22), 1);
        }
        gpio_wr(P(1,24), 1);
        power_on_late_done = 1;
    }

    /* 0x7316 */
    if (gpio_rd(P(1,30)) != 0)
        gpio_wr(P(1,29), 1);
    else
        gpio_wr(P(1,29), 0);

    timer_soft1_reset();                                      /* timer_soft[1] = 0 */
    power_on_late_done = 0;
    gpio_sel_p1_13_12(3);                                     /* P1_13 = 1, P1_12 = 1 */
    gpio_wr(P(0,6), 1);
}

/* 0x7364  callers: 5 */
void power_set_wake_reason(u8 reason)
{
    wake_reason = reason;
}

/* 0x7370  callers: 4 */
/* Power-off / standby entry.
 *   mode 1 = first call after reset (from main): skips the main-side
 *            teardown and cooling delay, initialises timers instead.
 *   mode 2 = fault shutdown (only difference inside: fn_9428(0) is skipped).
 *   other  = normal shutdown.
 * Three standby flavours at the end: eco, normal, WoL. */
void power_off(u8 mode)
{
    power_fault_armed = 0;
    power_is_on = 0;

    if (mode != 1) {
        /* ---- main-side teardown + cooling ---- */
        gpio_wr(P(1,8), 0);
        gpio_wr(P(1,10), 0);
        uart0_pins_release();
        gpio_wr(P(1,18), 0);
        delay_ticks(300);
        gpio_wr(P(1,15), 0);
        gpio_sel_p1_13_12(0);                                 /* P1_13 = 0, P1_12 = 0 */
        uart0_link_flag_clear();
        uart0_dev_clear_all();
        uart0_own_name_clear();
        gpio_wr(P(1,19), 0);
        gpio_wr(P(0,22), 0);
        gpio_wr(P(1,24), 0);
        gpio_wr(P(1,25), 0);
        fn_855c();                                  /* -> power_standby_pins() */
        gpio_sel_p2_21_20_n(0);                                 /* P2_21 = 1, P2_20 = 1 */
        gpio_wr(P(0,7), 1);
        delay_ticks(20000);
    }

    /* 0x7406 */
    gpio_init_table(mode);                  /* pin (re)configuration from rom_96b4; arg non-zero */

    if (mode == 1) {
        timer_systick_init();                  /* tick variables + SysTick setup */
        fn_2654();
    }

    ir_init();
    key_reset();
    i2c0_init();
    i2c1_init();
    i2c0_buf_ptr_init();
    i2c1_buf_init();
    delay_ticks(3000);

    power_state_code = 0;
    eeprom_select_a4();
    load_settings();

    /* 0x7444: USART1 handling */
    if (setting_keep_rails() != 0) {
        if (eeprom_ac_ok() == 1) {
            if (setting_wol_enabled() == 0)
                uart1_init(19200);
            /* else: WoL standby, USART1 not re-initialised */
        } else {
            uart1_init(19200);
        }
    } else {
        /* eco: release P0_13 / P0_14 */
        gpio_as_input(P(0,13), 0);
        gpio_as_input(P(0,14), 0);
        gpio_dir_clear(0, 13);             /* LPC_GPIO->DIR[0] &= ~(1 << 13) */
        gpio_dir_clear(0, 14);             /* LPC_GPIO->DIR[0] &= ~(1 << 14) */
        iocon_cfg(0, 13, 1);
        iocon_cfg(0, 14, 1);
    }

    /* 0x74a4 */
    uart2_init(19200);
    uart_tasks_init();
    eeprom_reload_factory_mode();                      /* -> eeprom_read_factory_mode() */
    power_standby_pins();
    gpio_wr(P(1,24), 0);
    gpio_wr(P(1,8), 0);

    power_last_off_mode = mode;

    /* power_state_code was zeroed at 0x7436; it can only be non-zero here
     * if one of the calls above (or an ISR) changed it. */
    if (power_state_code != 0 && power_state_code != 6 &&
        power_state_code != 4 && power_state_code != 7)
        return;                                     /* 0x74f0 */

    /* 0x74f2 */
    if (power_state_code == 0 && led_fault_latch == 0) {
        timer_hw_setup(0);                 /* r0 still holds led_fault_latch (== 0): CT32B1 */
        timer_hw_setup(1);                 /* CT16B1 */
        pwm_init(1000);              /* SCT0 setup */
        led_mode(0);
    }

    /* 0x751a */
    wake_reason = 0;

    if (g_02ed == 0 &&
        power_fault_code == 0 &&
        !(g_02ee == 1 && mode == 1) &&
        !(g_02f5 == 0 && mode == 1) &&
        !(power_state_code > 0 && power_state_code < 8) &&
        mode != 2)
    {
        fn_9428(0);                 /* no-op when called with 0 */
    }

    /* 0x7562 */
    power_fault_code = 0;
    timer_soft2_reset();                      /* timer_soft[2] = 0: restarts the standby-settle timer */
    standby_settled_flag = 0;

    if (setting_keep_rails() == 0) {
        /* ---------------- eco standby (0x757a) ---------------- */
        standby_rails_up = 0;
        gpio_wr(P(0,7), 0);
        gpio_wr(P(1,5), 0);
        gpio_wr(P(1,7), 0);
        gpio_wr(P(1,9), 0);

        if (eeprom_ac_ok() == 1 && setting_wol_enabled() == 1) {
            gpio_dir(P(0,3), 0);
            /* 5 x nop */
            gpio_wr(P(0,3), 0);
            phy_wol_state_set(0);             /* phy_wol_state = 0 */
            gpio_wr(P(1,29), 0);
            gpio_wr(P(1,30), 0);
        }
    } else {
        /* ---------------- rails kept (0x75ea) ---------------- */
        standby_rails_up = 1;

        if (eeprom_ac_ok() == 0) {
            /* no 0xAC EEPROM: normal standby */
            if (mode == 1) {
                gpio_wr(P(0,7), 1);
                gpio_wr(P(1,9), 1);
                gpio_wr(P(1,5), 1);
                gpio_wr(P(1,7), 1);
            }
        } else if (setting_wol_enabled() == 1) {
            /* ---- WoL standby (0x762c) ---- */
            gpio_dir(P(0,3), 0);
            /* 5 x nop */
            gpio_wr(P(0,3), 0);
            phy_wol_state_set(0);             /* phy_wol_state = 0 */
            gpio_wr(P(0,7), 1);
            gpio_wr(P(1,5), 1);
            gpio_wr(P(1,7), 1);
            delay_ticks(1000);
            gpio_wr(P(1,29), 1);
            gpio_wr(P(1,30), 1);
            gpio_wr(P(0,7), 0);
            gpio_wr(P(0,2), 1);
            gpio_wr(P(1,29), 0);
            gpio_wr(P(1,30), 0);
            phy_arm_wol();
            gpio_dir(P(0,3), 1);
            /* 5 x nop */
            gpio_wr(P(0,3), 1);
        } else {
            /* 0xAC EEPROM present, WoL off: normal standby (0x76c8) */
            if (mode == 1) {
                gpio_wr(P(0,7), 1);
                gpio_wr(P(1,9), 1);
                gpio_wr(P(1,5), 1);
                gpio_wr(P(1,7), 1);
            }
        }
    }
    /* 0x76f2 -> return */
}

/* 0x7730  callers: 1 */
/* Set or clear one bit of the EEPROM flag byte at 0x3e00 (read-modify-write). */
void eeprom_flag_bit_write(u8 bit, u8 set)
{
    u8 v;
    eeprom_read_byte(0x3e00, &v);
    if (set != 0)
        v = (u8)(v | (1 << bit));
    else
        v = v & ~(1 << bit);
    eeprom_write_byte(0x3e00, v);
}

/* 0x776e  callers: 5 */
/* 2-bit selector on P1_23 (MSB) / P1_22 (LSB). Values > 3: nothing. */
void gpio_sel_p1_23_22(u8 sel)
{
    switch (sel) {
    case 0: gpio_wr(P(1,23), 0); gpio_wr(P(1,22), 0); break;
    case 1: gpio_wr(P(1,23), 0); gpio_wr(P(1,22), 1); break;
    case 2: gpio_wr(P(1,23), 1); gpio_wr(P(1,22), 0); break;
    case 3: gpio_wr(P(1,23), 1); gpio_wr(P(1,22), 1); break;
    default: break;
    }
}

/* 0x77e0  callers: 11 */
/* 2-bit selector on P2_21 (MSB) / P2_20 (LSB), ACTIVE LOW (inverted compared
 * with gpio_sel_p1_23_22 / gpio_sel_p1_13_12). Values > 3: nothing. */
void gpio_sel_p2_21_20_n(u8 sel)
{
    switch (sel) {
    case 0: gpio_wr(P(2,21), 1); gpio_wr(P(2,20), 1); break;
    case 1: gpio_wr(P(2,21), 0); gpio_wr(P(2,20), 1); break;
    case 2: gpio_wr(P(2,21), 1); gpio_wr(P(2,20), 0); break;
    case 3: gpio_wr(P(2,21), 0); gpio_wr(P(2,20), 0); break;
    default: break;
    }
}

/* 0x7850  callers: 8 */
/* 2-bit selector on P1_13 (MSB) / P1_12 (LSB). Values > 3: nothing. */
void gpio_sel_p1_13_12(u8 sel)
{
    switch (sel) {
    case 0: gpio_wr(P(1,13), 0); gpio_wr(P(1,12), 0); break;
    case 1: gpio_wr(P(1,13), 0); gpio_wr(P(1,12), 1); break;
    case 2: gpio_wr(P(1,13), 1); gpio_wr(P(1,12), 0); break;
    case 3: gpio_wr(P(1,13), 1); gpio_wr(P(1,12), 1); break;
    default: break;
    }
}

/* 0x78c4  callers: 1 */
/* Build and send a short additive-checksum frame on USART1.
 * Template rom_96a8: 2A 20 01 01 01 03 50 00 00 00 00 00
 *   buf[0..1] = 2A 20 header, buf[2] = ?, buf[3] = ?, buf[4] = payload length,
 *   buf[5] = cmd, buf[6] = arg, last byte = sum of all preceding bytes.
 * Total length = buf[4] + 6. */
void uart1_send_2a20_frame(u8 cmd, u8 arg)
{
    u8 buf[12];
    u8 len = 7;                     /* overwritten below */
    u8 sum, i;

    memcpy(buf, rom_96a8, 12);

    switch (cmd) {                  /* rt_switch8, 16 entries */
    case 1: case 2: case 3: case 4:
    case 5: case 6: case 7: case 8:
    case 10: case 11:
        buf[2] = 1; buf[3] = 1; buf[4] = 1; buf[5] = cmd;
        break;
    case 9:
        buf[2] = 0; buf[3] = 1; buf[4] = 1; buf[5] = cmd;
        break;
    case 12:
        buf[2] = 1; buf[3] = 3; buf[4] = 2; buf[5] = cmd; buf[6] = arg;
        break;
    case 13: case 14:
        buf[2] = 1; buf[3] = 2; buf[4] = 1; buf[5] = cmd;
        break;
    case 15:
        buf[2] = 1; buf[3] = 3; buf[4] = 1; buf[5] = cmd;
        break;
    case 0:
    default:                        /* template sent unchanged */
        break;
    }

    len = (u8)(buf[4] + 6);
    sum = 0;
    for (i = 0; i < len - 1; i++)
        sum = (u8)(sum + buf[i]);
    buf[len - 1] = sum;

    delay_ticks(200);
    uart1_send(buf, len);              /* USART1 transmit */
}

/* 0x7998  callers: 1 */
/* Build and send a "BE EF" frame with CRC on USART4.
 * Template rom_9698: BE EF 10 06 00 00 00 33 33 02 00 01 FF 00 00 00,
 * followed by 24 zero bytes (40-byte buffer).
 *   buf[3] = length field (total = buf[3] + 7), buf[5..6] = CRC16 lo/hi,
 *   buf[9] = payload byte count, buf[11] = cmd, buf[12] = arg.
 * The CRC is computed over the whole frame with buf[5..6] still zero. */
void uart4_send_beef_frame(u8 cmd, u8 arg)
{
    u8  buf[40];
    u8  len = 13;
    u16 crc;

    rt_memclr4(buf + 16, 24);          /* rt memclr */
    memcpy(buf, rom_9698, 16);

    switch (cmd) {                  /* rt_switch8, 18 entries */
    case 1: case 2: case 3: case 4: case 5: case 6:
    case 7: case 8: case 9: case 10: case 11:
    case 13: case 14: case 15: case 16: case 17:
        buf[3]  = 5;
        buf[9]  = 1;
        buf[11] = cmd;
        len = (u8)(buf[3] + 7);     /* 12 */
        break;
    case 12:
        buf[3]  = 6;
        buf[9]  = 2;
        buf[11] = cmd;
        buf[12] = arg;
        len = (u8)(buf[3] + 7);     /* 13 */
        break;
    case 0:
    default:                        /* template, len 13 */
        break;
    }

    crc = crc16_modbus(buf, len);        /* table-driven CRC16 (rom_9488 / rom_9588) */
    buf[5] = (u8)crc;
    buf[6] = (u8)(crc >> 8);

    delay_ticks(200);
    fn_828c(buf, len);              /* USART4 transmit (-> uart4_send_blocking) */
}

/* 0x7a48  callers: 4 */
/* Build and send a "BE EF" frame with CRC on USART1; cmd is one of
 * 0xD1 / 0xD2 / 0xD5 / 0xDA.  Template rom_9688 (identical bytes to
 * rom_9698) + 24 zero bytes.  Field layout as in uart4_send_beef_frame.
 * NOTE: the CRC covers `len` bytes but the transmit call always sends 40. */
void uart1_send_beef_status(u8 cmd, u8 arg)
{
    u8  buf[40];
    u32 len = 13;
    u32 tmp = 0;                    /* filled by setting_get_1a18, never read */
    u32 zero = 0;                   /* r6: constant 0 throughout */
    u16 crc;

    rt_memclr4(buf + 16, 24);          /* rt memclr */
    memcpy(buf, rom_9688, 16);

    if (cmd == 0xd1) {
        buf[3]  = 0x0b;
        buf[9]  = 7;
        buf[11] = 0xd1;
        len = (u8)(buf[3] + 7);     /* 18 */
    } else if (cmd == 0xd2) {
        buf[3]  = 0x1a;
        buf[9]  = 0x16;
        buf[11] = 0xd2;
        buf[12] = 0;
        switch (led_fault_latch) {           /* rt_switch8 at 0x7a9a, 6 entries */
        case 0:  buf[13] = 0x55; break;
        case 3:  buf[13] = 3;    break;
        case 2:  buf[13] = 4;    break;
        case 4:
        case 5:  buf[13] = 7;    break;
        case 1:
        default: buf[13] = 1;    break;
        }
        setting_get_1a18(&tmp);              /* tmp = g_1a18 (u32 at 0x10001a18); unused */
        buf[14] = (u8)(zero >> 8);  /* always 0 */
        buf[15] = (u8)zero;         /* always 0 */
        buf[16] = 0;
        buf[17] = 0;
        buf[18] = 0;
        buf[19] = 0xaa;
        buf[20] = 1;
        buf[21] = 0xaa;
        buf[22] = 0;
        buf[23] = 0;
        buf[24] = 0;
        buf[25] = 0xaa;
        buf[26] = 0;
        buf[27] = 0xaa;
        buf[28] = 1;
        buf[29] = 0xaa;
        buf[30] = 1;
        buf[31] = 0;
        buf[32] = 0;
        len = (u8)(buf[3] + 7);     /* 33 */
    } else if (cmd == 0xd5) {
        buf[11] = 0xd5;
        buf[12] = arg;
        len = (u8)(buf[3] + 7);     /* template buf[3] = 6 -> 13 */
    } else if (cmd == 0xda) {
        buf[3]  = 0x0c;
        buf[9]  = 8;
        buf[11] = 0xda;
        if (setting_classify_1a0c() == 0x55)
            buf[12] = 0x17;
        else if (setting_classify_1a0c() == 0xaa)
            buf[12] = 0x18;
        /* else buf[12] stays 0xFF from the template */
        len = (u8)(buf[3] + 7);     /* 19 */
    }
    /* any other cmd: template, len 13 */

    crc = crc16_modbus(buf, len);
    buf[5] = (u8)crc;
    buf[6] = (u8)(crc >> 8);

    delay_ticks(200);
    uart1_send(buf, 0x28);             /* USART1 transmit, always 40 bytes */
}

/* 0x7bbc  callers: 3 */
void power_set_flag_02e6(u8 v)
{
    g_02e6 = v;
}

/* 0x7bc8  callers: 2 */
void power_set_flag_02e5(u8 v)
{
    g_02e5 = v;
}

/* 0x7bd4  callers: 0 (vector) */
/* SysTick handler: free-running tick, eight up-counting software timers,
 * two down-counters (timer_down_counter and the delay_ticks counter). */
void timer_systick_isr(void)                  /* SysTick vector */
{
    u8 i;

    timer_tick_count++;                       /* u32 */
    gpio_pulse_tick();

    for (i = 0; i < 8; i++)
        timer_soft[i]++;                /* u32[8] */

    if (timer_down_counter != 0)
        timer_down_counter--;                   /* u32 */
    if (delay_counter != 0)
        delay_counter--;
}

/* 0x7c34  callers: 0 (vector) */
/* CT16B1 match-0 interrupt (period = 100 timer counts at PR = 47, see
 * timer_hw_setup).  One-element timeout counter ir_tick[] with limit ir_timeout_ticks, then
 * /100 and /100 prescalers feeding several u32 up-counters. */
void timer_ct16b1_isr(void)                  /* CT16B1 vector */
{
    u8 i;

    if ((LPC_CT16B1->IR & 1) == 0)
        return;
    LPC_CT16B1->IR = 1;

    for (i = 0; i < 1; i++) {
        if (ir_tick[i] <= ir_timeout_ticks) {              /* u16[1] vs u16 limit */
            ir_tick[i]++;
            if (ir_tick[i] > ir_timeout_ticks)
                ir_on_timeout(i);                     /* timeout: resets IR-decoder state ? */
        }
    }

    timer_ct16b1_div100++;                                   /* u8 */
    if (timer_ct16b1_div100 < 100)
        return;
    timer_ct16b1_div100 = 0;

    timer_cnt100_a++;                                   /* u32, every 100 interrupts */
    timer_cnt100_b++;
    timer_cnt100_c++;
    timer_cnt100_d++;

    timer_ct16b1_div10000++;                                   /* u8 */
    if (timer_ct16b1_div10000 < 100)
        return;
    timer_ct16b1_div10000 = 0;

    timer_slow_count++;                                   /* u32, every 10000 interrupts */
}

/* 0x7d0c  callers: 0 (vector) */
/* CT32B0 interrupt.  Match-0: five overflow counters.  Capture-0 (IR bit 4):
 * measures the time between edges on P2_9.  Low level -> duration since the
 * last "high" edge goes into ct32b0_high_log[]; high level -> duration since the last
 * "low" edge goes into ct32b0_low_log[].  Timer period assumed 24000 counts.
 * Both are 99-entry circular u32 logs (index wraps to 0 on reaching 99).
 * Note it samples TC (offset 0x08), not CR0. */
void timer_ct32b0_isr(void)                  /* CT32B0 vector */
{
    if (LPC_CT32B0->IR & 1) {
        LPC_CT32B0->IR = 1;
        ct32b0_overflow_count++;                               /* u32 overflow count used below */
        g_021c++;
        g_0220++;
        g_0224++;
        g_0228++;
    }

    if (LPC_CT32B0->IR & 0x10) {
        LPC_CT32B0->IR = 0x10;

        if (gpio_rd(P(2,9)) == 0) {
            ct32b0_tc_at_low = LPC_CT32B0->TC;
            ct32b0_high_width = ct32b0_overflow_count * 24000 + ct32b0_tc_at_low - ct32b0_tc_at_high;
            ct32b0_overflow_count = 0;
            ct32b0_high_log[g_022c[3]] = ct32b0_high_width;         /* u32 ct32b0_high_log[]; index byte at 0x1000022f */
            g_022c[3]++;
            if (g_022c[3] == 99)
                g_022c[3] = 0;
        } else {
            ct32b0_tc_at_high = LPC_CT32B0->TC;
            ct32b0_low_width = ct32b0_overflow_count * 24000 + ct32b0_tc_at_high - ct32b0_tc_at_low;
            ct32b0_overflow_count = 0;
            ct32b0_low_log[g_0232[3]] = ct32b0_low_width;         /* u32 ct32b0_low_log[]; index byte at 0x10000235 */
            g_0232[3]++;
            if (g_0232[3] == 99)
                g_0232[3] = 0;
        }
    }
}

/* 0x7e48  callers: 0 (vector) */
/* CT32B1 match-0 interrupt (period = 50000 timer counts at PR = 47). */
void timer_ct32b1_isr(void)                  /* CT32B1 vector */
{
    if ((LPC_CT32B1->IR & 1) == 0)
        return;
    LPC_CT32B1->IR = 1;

    ct32b1_tick_count++;                                   /* u16 */
    led_tick(ct32b1_tick_count);
    if (tone_on != 0)
        tone_tick();
    key_hold_ticks++;                                   /* u16 */
}

/* 0x7e98  callers: 1 */
/* Reset timeout counter ir_tick[idx].  Return value is whatever was in r2 on
 * entry (uninitialised) -- effectively void. */
u16 timer_timeout_reset(u8 idx)
{
    SYSTICK_CSR_TOUCH();
    ir_tick[idx] = 0;
    SYSTICK_CSR_TOUCH();
    return /* ? garbage (r2) */ 0;
}

/* 0x7ec0  callers: 3 */
void timer_soft0_reset(void)
{
    SYSTICK_CSR_TOUCH();
    timer_soft[0] = 0;
    SYSTICK_CSR_TOUCH();
}

/* 0x7ee0  callers: 1 */
u32 timer_soft1_get(void)
{
    return timer_soft[1];
}

/* 0x7eec  callers: 1 */
/* tick count since standby entry (read by standby_settled) */
u32 timer_soft2_get(void)
{
    return timer_soft[2];
}

/* 0x7ef8  callers: 1 */
u32 timer_soft4_get(void)
{
    return timer_soft[4];
}

/* 0x7f04  callers: 1 */
u32 timer_soft5_get(void)
{
    return timer_soft[5];
}

/* 0x7f10  callers: 1 */
u32 timer_soft3_get(void)
{
    return timer_soft[3];
}

/* 0x7f1c  callers: 1 */
u16 timer_timeout_get(u8 idx)
{
    return ir_tick[idx];
}

/* 0x7f2c  callers: 2 */
/* low 16 bits of software timer 0 */
u16 timer_soft0_get16(void)
{
    return (u16)timer_soft[0];
}

/* 0x7f38  callers: 1 */
/* Tick subsystem init: zero all tick variables, fetch the ir_tick[] timeout
 * limit, then inline CMSIS SysTick_Config(4800). */
void timer_systick_init(void)
{
    u8  i;
    u32 ticks;

    timer_tick_count = 0;
    timer_down_counter = 0;
    for (i = 0; i < 8; i++)
        timer_soft[i] = 0;
    for (i = 0; i < 1; i++)
        ir_tick[i] = 0;
    delay_counter = 0;

    ir_timeout_ticks = get_ir_timeout_ticks();             /* constant 0x93 (147) */

    ticks = 0x4b << 6;              /* 4800 */
    if (ticks - 1 <= 0x00ffffff) {  /* always true */
        REG32(0xE000E014) = ticks - 1;      /* SysTick->LOAD = 4799 */
        nvic_set_priority(-1, 3);                     /* NVIC_SetPriority(SysTick_IRQn, 3) */
        REG32(0xE000E018) = 0;              /* SysTick->VAL  */
        REG32(0xE000E010) = 7;              /* SysTick->CTRL: enable, tickint, core clock */
    }
}

/* 0x7fc4  callers: 1 */
void timer_soft1_reset(void)
{
    SYSTICK_CSR_TOUCH();
    timer_soft[1] = 0;
    SYSTICK_CSR_TOUCH();
}

/* 0x7fe4  callers: 1 */
void timer_soft2_reset(void)
{
    SYSTICK_CSR_TOUCH();
    timer_soft[2] = 0;
    SYSTICK_CSR_TOUCH();
}

/* 0x8004  callers: 1 */
void timer_soft4_reset(void)
{
    SYSTICK_CSR_TOUCH();
    timer_soft[4] = 0;
    SYSTICK_CSR_TOUCH();
}

/* 0x8024  callers: 1 */
void timer_soft5_reset(void)
{
    SYSTICK_CSR_TOUCH();
    timer_soft[5] = 0;
    SYSTICK_CSR_TOUCH();
}

/* 0x8044  callers: 1 */
void timer_soft3_reset(void)
{
    SYSTICK_CSR_TOUCH();
    timer_soft[3] = 0;
    SYSTICK_CSR_TOUCH();
}

/* 0x8064  callers: 2 */
/* Hardware timer setup, selected by `which`.  Only 0 and 1 are ever passed
 * from this range (power_off); case 3 has no caller here and does not
 * enable an interrupt. */
void timer_hw_setup(u8 which)
{
    switch (which) {
    case 0:                                         /* CT32B1 */
        LPC_SYSCON->SYSAHBCLKCTRL |= (1u << 10);
        LPC_CT32B1->PR  = 0x2f;                     /* /48 */
        LPC_CT32B1->MR0 = 0xc34f;                   /* 49999 */
        LPC_CT32B1->MCR = 3;                        /* interrupt + reset on MR0 */
        LPC_CT32B1->TC  = 0;
        LPC_CT32B1->TCR = 1;
        nvic_enable_irq_c(0x13);                              /* NVIC enable IRQ 19 (CT32B1) */
        break;

    case 1:                                         /* CT16B1 */
        LPC_SYSCON->SYSAHBCLKCTRL |= (1u << 8);
        LPC_CT16B1->PR  = 0x2f;                     /* /48 */
        LPC_CT16B1->MR0 = 0x63;                     /* 99 */
        LPC_CT16B1->MCR = 3;
        LPC_CT16B1->TC  = 0;
        LPC_CT16B1->TCR = 1;
        nvic_enable_irq_c(0x11);                              /* NVIC enable IRQ 17 (CT16B1) */
        break;

    case 3:                                         /* CT32B0, free-running/PWM, no IRQ */
        LPC_SYSCON->SYSAHBCLKCTRL |= (1u << 9);
        REG32(0x40014074) = 2;                      /* CT32B0 PWMC: PWM enable ch1 ? */
        REG32(0x40014024) = 0x8e42;                 /* CT32B0 MR3 = 36418 */
        REG32(0x4001401c) = 0x8e42;                 /* CT32B0 MR1 = 36418 */
        LPC_CT32B0->MCR = 0x400;                    /* reset on MR3 */
        LPC_CT32B0->TC  = 0;
        LPC_CT32B0->TCR = 1;
        break;

    case 5:
    default:
        break;
    }
}

/* 0x8120  callers: 5 */
void timer_ct32b1_count_reset(void)
{
    ct32b1_tick_count = 0;                     /* CT32B1 interrupt counter */
}

/* 0x812c  callers: 2 */
void rt_nop(void)
{
    /* empty: bx lr */
}

/* 0x8130  callers: 61 */
/* Busy-wait for n SysTick interrupts (delay_counter is decremented by
 * timer_systick_isr).  See the tick-rate caveat at the top of this file. */
void delay_ticks(u32 n)
{
    SYSTICK_CSR_TOUCH();
    delay_counter = n;
    SYSTICK_CSR_TOUCH();
    while (delay_counter != 0)
        ;
}

/* 0x8158  callers: 9 */
/* USART0: discard received data (IRQ 21 masked while clearing). */
void uart0_rx_reset(void)
{
    nvic_disable_irq(0x15);                  /* NVIC->ICER = 1 << 21 */
    uart0_rx_count = 0;                     /* u32 rx count */
    g_0243 = 0;                     /* u8 */
    nvic_enable_irq(0x15);                  /* NVIC->ISER = 1 << 21 */
}

/* 0x817c  callers: 5 */
u8 *get_uart0_rx_buf(void)
{
    return uart0_rx_buf;                  /* USART0 rx buffer */
}

/* 0x8184  callers: 6 */
u32 get_uart0_rx_count(void)
{
    return uart0_rx_count;                  /* USART0 rx count */
}

/* 0x8190  callers: 13 */
void uart0_send(u8 *buf, u32 len)
{
    uart0_send_blocking(buf, len);              /* USART0 blocking transmit */
}

/* 0x81a0  callers: 3 */
/* USART1: discard received data (IRQ 11 = USART1/4 masked while clearing). */
void uart1_rx_reset(void)
{
    nvic_disable_irq(0x0b);
    uart1_rx_count = 0;                     /* u32 rx count */
    uart1_rx_flag = 0;                     /* u8 */
    nvic_enable_irq(0x0b);
}

/* 0x81c4  callers: 1 */
u8 *get_uart1_rx_buf(void)
{
    return uart1_rx_buf;                  /* USART1 rx buffer */
}

/* 0x81cc  callers: 2 */
u32 get_uart1_rx_count(void)
{
    return uart1_rx_count;                  /* USART1 rx count */
}

/* 0x81d8  callers: 1 */
u8 get_uart1_rx_flag(void)
{
    return uart1_rx_flag;                  /* USART1 rx flag/state byte */
}

/* 0x81e4  callers: 3 */
void uart1_send(u8 *buf, u32 len)
{
    uart1_send_blocking(buf, len);              /* USART1 blocking transmit */
}

/* 0x81f4  callers: 6 */
/* USART2: discard received data (IRQ 12 = USART2/3 masked while clearing). */
void uart2_rx_reset(void)
{
    nvic_disable_irq(0x0c);
    uart2_rx_count = 0;                     /* u32 rx count */
    uart2_rx_flag = 0;                     /* u8 */
    nvic_enable_irq(0x0c);
}

/* 0x8218  callers: 1 */
u8 *get_uart2_rx_buf(void)
{
    return uart2_rx_buf;                  /* USART2 rx buffer */
}

/* 0x8220  callers: 2 */
u32 get_uart2_rx_count(void)
{
    return uart2_rx_count;                  /* USART2 rx count */
}

/* 0x822c  callers: 1 */
u8 get_uart2_rx_flag(void)
{
    return uart2_rx_flag;                  /* USART2 rx flag/state byte */
}

/* 0x8238  callers: 11 */
void uart2_send(u8 *buf, u32 len)
{
    uart2_send_blocking(buf, len);              /* USART2 blocking transmit */
}

/* 0x8248  callers: 4 */
/* USART4: discard received data (IRQ 11 = USART1/4 masked while clearing). */
void uart4_rx_reset(void)
{
    nvic_disable_irq(0x0b);
    uart4_rx_count = 0;                     /* u32 rx count */
    uart4_rx_flag = 0;                     /* u8 */
    nvic_enable_irq(0x0b);
}


/* ======================================================================
 * Part 7
 * ====================================================================== */

/*
 * part_7.c -- LPC11U67 application, 0x826c .. 0x9a00
 *
 * USART4 ring accessors, USART0 / USART1+4 / USART2+3 IRQ handlers,
 * power_on_rails, EEPROM address helper, main, bit-banged MDIO, CRC16,
 * and the constant data that ends the image.
 *
 * Pin ids are (port<<8)|pin, written P(port,pin).
 * MDIO pins:  MDIO_DATA = P(1,30) (0x11e),  MDIO_CLK = P(1,29) (0x11d).
 *
 * Register naming:
 *   USART0 (0x40008000) is the 16550-style block: RBR +0x00, IIR +0x08, LSR +0x14.
 *   USART1..4 (0x4006c000, 0x40070000, 0x40074000, 0x4004c000) are the
 *   LPC8xx-style block: STAT +0x08, INTENCLR +0x10, RXDAT +0x14.
 */

/* ------------------------------------------------------------------ */
/* USART4 receive-buffer accessors (tail of the accessor group that    */
/* starts before this range: 0x8248 is the matching "reset" function)  */
/* ------------------------------------------------------------------ */

/* 0x826c  callers: 1 */
/* Returns the address of the USART4 receive buffer. */
u8 *get_uart4_rx_buf(void)
{
    return uart4_rx_buf;
}

/* 0x8274  callers: 3 */
/* Returns the USART4 receive byte count / write index. */
u32 get_uart4_rx_count(void)
{
    return uart4_rx_count;                      /* u32 */
}

/* 0x8280  callers: 1 */
/* Returns the byte flag uart4_rx_flag (cleared together with uart4_rx_count by uart4_rx_reset;
 * not written anywhere in this range). */
u8 fn_8280(void)
{
    return uart4_rx_flag;
}

/* 0x828c  callers: 1 */
/* Thin wrapper: forwards both arguments unchanged to uart4_send_blocking.
 * By position in the accessor group (0x8190->0x42c8, 0x81e4->0x4304,
 * 0x8238->0x433c, 0x828c->0x4374) this is a per-UART "send(buf, len)"  ? */
u32 fn_828c(u32 a, u32 b)
{
    return uart4_send_blocking(a, b);
}

/* ------------------------------------------------------------------ */
/* UART interrupt handlers                                             */
/* ------------------------------------------------------------------ */

/* 0x829c  callers: 0 (vector) */
/* USART0 IRQ.  Line-oriented receive into uart0_rx_buf[]: every byte is stored
 * at uart0_rx_buf[uart0_rx_count]; the index advances unless the byte is 0x0A, in which
 * case uart0_rx_line_done ("line complete") is set and the index is left pointing at
 * the LF.  NOTE: there is no bound check on uart0_rx_count here (buffer is 0x40
 * bytes, 0x100018c0..0x100018ff, followed by the USART1 buffer). */
void uart0_irq(void)
{
    u8 id, lsr;                                   /* lsr is read and, for id 3, discarded */

    id = ((u8)LPC_USART0->IIR >> 1) & 7;          /* interrupt id field */

    if (id == 3) {                                /* receive line status */
        lsr = (u8)LPC_USART0->LSR;                /* read clears it */
    } else if (id == 2) {                         /* receive data available */
        uart0_rx_buf[uart0_rx_count] = (u8)LPC_USART0->RBR;
        uart0_rx_activity = 1;
        if (uart0_rx_buf[uart0_rx_count] != 0x0a) {
            uart0_rx_count = uart0_rx_count + 1;
        } else {
            uart0_rx_line_done = 1;
        }
    } else if (id == 6) {                         /* character timeout */
        g_0242 = g_0242;                          /* sic: reads the byte and writes it back */
    } else if (id == 1) {                         /* THR empty */
        lsr = (u8)LPC_USART0->LSR;
        if (lsr & 0x20) {
            uart0_tx_empty = 1;
        } else {
            uart0_tx_empty = 0;
        }
    }
}

/* 0x833c  callers: 0 (vector) */
/* Shared USART1 / USART4 IRQ.  Both halves are identical apart from the
 * register block and the RAM cells.  Receive: byte -> buf[count]; if the
 * byte is 0x50 ('P') a flag is set and the stored byte is replaced by 0;
 * count++; when count reaches 0x40 a "wrapped" flag is set and count = 0.
 * Transmit-ready: set a flag and write bit 2 to INTENCLR. */
void uart1_4_irq(void)
{
    u32 stat;

    /* ---- USART1 ---- */
    stat = LPC_USART1->STAT;
    if (stat & 1) {                               /* RXRDY */
        uart1_rx_buf[uart1_rx_count] = (u8)LPC_USART1->RXDAT;
        if (uart1_rx_buf[uart1_rx_count] == 0x50) {
            uart1_rx_got_p = 1;
            uart1_rx_buf[uart1_rx_count] = 0;
        }
        uart1_rx_count = uart1_rx_count + 1;
        if (uart1_rx_count == 0x40) {
            uart1_rx_wrapped = 1;
            uart1_rx_count = 0;
        }
    }
    if (stat & 4) {                               /* TXRDY */
        uart1_tx_ready = 1;
        LPC_USART1->INTENCLR = LPC_USART1->INTENCLR | 4;   /* sic: read-modify-write */
    }

    /* ---- USART4 ---- */
    stat = LPC_USART4->STAT;
    if (stat & 1) {
        uart4_rx_buf[uart4_rx_count] = (u8)LPC_USART4->RXDAT;
        if (uart4_rx_buf[uart4_rx_count] == 0x50) {
            uart4_rx_got_p = 1;
            uart4_rx_buf[uart4_rx_count] = 0;
        }
        uart4_rx_count = uart4_rx_count + 1;
        if (uart4_rx_count == 0x40) {
            uart4_rx_wrapped = 1;
            uart4_rx_count = 0;
        }
    }
    if (stat & 4) {
        uart4_tx_ready = 1;
        LPC_USART4->INTENCLR = LPC_USART4->INTENCLR | 4;
    }
}

/* 0x8448  callers: 0 (vector) */
/* Shared USART2 / USART3 IRQ.  Same code as uart1_4_irq with other cells. */
void uart2_3_irq(void)
{
    u32 stat;

    /* ---- USART2 ---- */
    stat = LPC_USART2->STAT;
    if (stat & 1) {
        uart2_rx_buf[uart2_rx_count] = (u8)LPC_USART2->RXDAT;
        if (uart2_rx_buf[uart2_rx_count] == 0x50) {
            uart2_rx_got_p = 1;
            uart2_rx_buf[uart2_rx_count] = 0;
        }
        uart2_rx_count = uart2_rx_count + 1;
        if (uart2_rx_count == 0x40) {
            uart2_rx_wrapped = 1;
            uart2_rx_count = 0;
        }
    }
    if (stat & 4) {
        uart2_tx_ready = 1;
        LPC_USART2->INTENCLR = LPC_USART2->INTENCLR | 4;
    }

    /* ---- USART3 ---- */
    stat = LPC_USART3->STAT;
    if (stat & 1) {
        uart3_rx_buf[uart3_rx_count] = (u8)LPC_USART3->RXDAT;
        if (uart3_rx_buf[uart3_rx_count] == 0x50) {
            uart3_rx_got_p = 1;
            uart3_rx_buf[uart3_rx_count] = 0;
        }
        uart3_rx_count = uart3_rx_count + 1;
        if (uart3_rx_count == 0x40) {
            uart3_rx_wrapped = 1;
            uart3_rx_count = 0;
        }
    }
    if (stat & 4) {
        uart3_tx_ready = 1;
        LPC_USART3->INTENCLR = LPC_USART3->INTENCLR | 4;
    }
}

/* ------------------------------------------------------------------ */

/* 0x8554  callers: 1 (power_off, 0x74b0) */
/* Wrapper: re-read the factory-mode byte from the EEPROM. */
void eeprom_reload_factory_mode(void)
{
    eeprom_read_factory_mode();
}

/* 0x855c  callers: 1 (power_off, 0x73ee) */
/* Wrapper around power_standby_pins. */
void fn_855c(void)
{
    power_standby_pins();
}

/* 0x8564  callers: 1 (power_on, 0x72c8) */
/* Power-on rail sequencer.  Raises the main supply, restores the standby
 * rail group depending on how standby was entered, then sequences the
 * main-side rails.  If setting_s2_is_zero() is true it additionally runs a two-pass
 * P2_23 / P1_15 / P1_18 sequence with 10 s holds. */
void power_on_rails(void)
{
    gpio_wr(P(1,25), 1);

    if (eeprom_ac_ok() == 1) {
        if (setting_wol_enabled() == 1) {
            gpio_wr(P(0,3), 0);
            if (phy_wol_state_get() != 0) {
                phy_leave_wol();
                gpio_wr(P(1,9), 1);
            } else {
                gpio_wr(P(0,7), 0);
                delay_ticks(1000);
                gpio_wr(P(1,9), 1);
                gpio_wr(P(1,5), 1);
                gpio_wr(P(1,7), 1);
            }
            gpio_wr(P(0,2), 0);
        } else if (standby_rails_up == 0) {
            gpio_wr(P(0,7), 0);
            delay_ticks(1000);
            gpio_wr(P(1,9), 1);
            gpio_wr(P(1,5), 1);
            gpio_wr(P(1,7), 1);
        }
    } else if (standby_rails_up == 0) {
        gpio_wr(P(0,7), 0);
        delay_ticks(1000);
        gpio_wr(P(1,9), 1);
        gpio_wr(P(1,5), 1);
        gpio_wr(P(1,7), 1);
    }

    delay_ticks(5000);
    if (fn_48fc() == 0) {
        gpio_sel_p2_21_20_n(2);
    }
    delay_ticks(7000);
    gpio_wr(P(1,24), 1);
    if (fn_48fc() == 0) {
        led_mode(0x16);
    }
    if (fn_48fc() == 0) {
        gpio_wr(P(1,19), 1);
    }
    gpio_wr(P(1,9), 1);
    if (fn_48fc() == 0) {
        gpio_wr(P(0,21), 0);
    }
    gpio_sel_p1_23_22(2);
    gpio_wr(P(1,31), 1);
    if (factory_test3_active == 0 && factory_test10_active == 0) {             /* neither factory test 3 nor 0x10 active */
        gpio_wr(P(2,17), 1);
    }
    if (fn_48fc() == 0) {
        gpio_wr(P(0,8), 1);
    }
    gpio_wr(P(1,10), 1);

    if (setting_s2_is_zero() != 0) {
        uart0_pins_attach();
        uart0_rx_reset();
        gpio_wr(P(2,23), 1);
        delay_ticks(300);
        gpio_wr(P(1,15), 1);
        delay_ticks(500);
        gpio_wr(P(1,18), 1);
        delay_ticks(10000);
        gpio_wr(P(2,23), 0);
        delay_ticks(10000);
        uart0_send_frame_05_03();
        uart0_pins_release();
        gpio_wr(P(1,18), 0);
        delay_ticks(300);
        gpio_wr(P(1,15), 0);
        uart0_link_flag_clear();
        uart0_dev_clear_all();
        uart0_own_name_clear();
        uart0_pins_attach();
        uart0_rx_reset();
        gpio_wr(P(2,23), 1);
        delay_ticks(300);
        gpio_wr(P(1,15), 1);
        delay_ticks(500);
        gpio_wr(P(1,18), 1);
        delay_ticks(10000);
        gpio_wr(P(2,23), 0);
        delay_ticks(10000);
        uart0_send_frame_05_03();
    }
}

/* 0x87d8  callers: 1 (eeprom_reload_factory_mode) */
/* Reads EEPROM cell 0x3e06 (factory mode) into factory_mode. */
u32 eeprom_read_factory_mode(void)
{
    return eeprom_read_byte(0x3e06, &factory_mode);
}

/* 0x87ec  callers: 11 */
/* Keil runtime: __ARM_common_switch8.  Byte table follows the BL; first
 * byte is the case count, index is clamped to it (default = last entry),
 * entries are (target - table) / 2. */
void rt_switch8(void);

/* 0x8808  callers: 2 (eeprom_read_byte 0x6504, eeprom_write_byte 0x656c) */
/* Splits a linear EEPROM address into I2C device address + in-device
 * address bytes, using the geometry entry rom_9934[eeprom_type_idx]
 * (6 bytes: u8 two_byte_addr, pad, u16 block_size, u8 n_blocks, pad).
 * Returns 0 if addr is beyond the device, 1 otherwise.
 *   *dev  = I2C device address byte
 *   buf[] = 1 or 2 address bytes,  *len = how many */
struct rom_9934_entry { u8 two_byte_addr; u8 pad0; u16 block_size; u8 n_blocks; u8 pad1; };

u32 eeprom_addr_split(u32 addr, u8 *dev, u8 *buf, u8 *len)
{
    const struct rom_9934_entry *e = &rom_9934[eeprom_type_idx];   /* re-indexed on every use */
    u8 hi, lo, n;

    if (!((s32)(e->block_size * e->n_blocks) > (s32)addr)) {
        return 0;
    }

    *dev = eeprom_i2c_addr;
    hi = (u8)(addr / e->block_size);              /* rt_idivmod, quotient  */
    lo = (u8)(addr % e->block_size);              /* rt_idivmod, remainder (called twice) */

    if (e->n_blocks > 1 && hi > 0 && e->two_byte_addr == 0) {
        /* block number folded into the device address.
         * sic: uses (hi - 1), not hi */
        *dev = *dev + (((hi - 1) & 7) << 1);
    }

    n = 0;
    if (e->two_byte_addr != 0) {
        buf[n++] = hi;
    }
    buf[n++] = lo;
    *len = n;
    return 1;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

/* 0x88d8  callers: 1 (__main, 0x2132) */
/* Never returns.
 *
 * Registers that live across loop iterations:
 *   ir   (r4)  last IR code taken; NOT initialised before the loop and only
 *              cleared on the "on" path and after an IR power-on
 *   i    (r5)  blink loop counter in factory tests 8 / 0xb
 *   wake (r6)  wake request
 *   uart (r7)  result of uart_wake_token_take()
 * Stack locals: stage (sp+0), keys (sp+4), ir_aux (sp+8).
 *
 * Top-level dispatch per iteration:
 *   power_get_state_code() != 0             -> (re)start request: power_on if not yet on
 *   power_get_is_on() != 0             -> projector is ON  (0x8f18)
 *   otherwise                  -> projector is OFF / standby (0x8952)
 *
 * The factory-test branches are NOT a one-shot at reset: they sit in the
 * OFF path and are entered whenever P2_15 reads high and P1_16 (power key)
 * reads low.  While the key stays low the code loops; once key_hold_ticks >= 60
 * an IR code selects a test.  Most tests end in an endless loop.
 */
void main(void)
{
    u32 stage = 0;
    u32 keys;
    u32 ir_aux = 0;
    u32 ir;        /* r4, uninitialised */
    u8  i;         /* r5 */
    u32 wake;      /* r6, uninitialised */
    u32 uart;      /* r7 */

    clock_init(1, 1, 3, 1);
    power_off(1);

    LPC_SYSCON->SYSAHBCLKCTRL &= ~(1u << 27);     /* bit 27: USB RAM clock ? */
    LPC_SYSCON->PDRUNCFG |= 3;                    /* 0x40048238: IRC out + IRC powered down */

    for (;;) {                                    /* 0x890e */
        key_power_debounce();                                /* power-key debounce */
        uart2_task();                                /* USART2 task */
        uart1_task();                                /* USART1 task */
        uart4_task();                                /* USART4 task */
        i2c0_cmd_task();
        i2c1_cmd_task();                                /* I2C1 slave command task */
        uart0_task();                                /* USART0 task */

        if (power_get_state_code() != 0) {
            if (power_get_is_on() == 0) {
                power_on();
                power_set_wake_reason(5);                       /* wake reason 5 */
                power_on_late_task();
            }
            continue;
        }

        if (power_get_is_on() != 0) {
            /* ================= ON state (0x8f18) ================= */
            if (fn_48fc() != 0) {
                if (gpio_rd(P(0,9)) != 0) {
                    power_set_flag_02e6(0);
                    power_off_normal();
                    power_clear_on_flags();
                }
            } else {
                power_fault_monitor();                        /* on-state monitor (fault pin P1_27) */
            }
            power_on_late_task();

            if (ir_idle_flag(1) != 0 && pulse_state == 0 && power_on_late_done == 1) {
                ir = ir_take_code();
                if (ir == 0xf0 && ir_addr_is_ffff != 0) {
                    pulse_start(0, 0x14, 0x14, 3);
                } else if (ir == 0xe2 && ir_addr_is_ffff != 0) {
                    pulse_start(1, 0x14, 0x14, 3);
                }
                ir_take_code();                   /* result discarded */
            }
            ir = 0;
            wake = 0;
            continue;
        }

        /* ================= OFF / standby state (0x8952) ================= */
        keys = (gpio_rd(P(2,15)) == 0) ? 1 : 0;
        keys = (u8)((keys << 1) | ((gpio_rd(P(1,16)) == 0) ? 1 : 0));

        if (keys == 1) {                          /* P2_15 high, P1_16 low */
            key_hold_ticks = 0;                           /* u16 */
            factory_test_armed = 0;

            while (gpio_rd(P(1,16)) == 0) {       /* 0x8e5a: as long as the key is held */

                if (key_hold_ticks >= 0x3c && factory_test_armed == 0) {
                    LPC_SYSCON->SYSAHBCLKCTRL |= (1u << 16);   /* IOCON clock ? */
                    factory_test_armed = 1;
                    led_mode(0x12);
                    continue;
                }
                if (key_hold_ticks < 0x3c) {
                    ir_take_code();               /* 0x8e56: drain, result discarded */
                    continue;
                }

                ir = ir_take_code();

                if (ir == 0x17) {
                    led_mode(0x17);               /* r0 still holds the IR code == 0x17 */
                    gpio_wr(P(2,10), 1);
                    gpio_wr(P(2,9), 1);
                    gpio_sel_p2_21_20_n(3);
                    delay_ticks(6000);
                    gpio_wr(P(1,25), 1);
                    delay_ticks(7000);
                    gpio_wr(P(1,19), 1);
                    gpio_wr(P(1,31), 1);
                    gpio_wr(P(0,23), 0);
                    gpio_wr(P(0,8), 1);
                    for (;;) { }                  /* 0x8a2a */
                }
                else if (ir == 0x11) {
                    led_mode(0x17);
                    gpio_wr(P(2,10), 1);
                    gpio_wr(P(2,9), 1);
                    gpio_wr(P(2,18), 1);
                    gpio_sel_p2_21_20_n(3);
                    delay_ticks(6000);
                    gpio_wr(P(1,25), 1);
                    delay_ticks(7000);
                    gpio_wr(P(1,9), 0);
                    gpio_wr(P(0,11), 1);
                    delay_ticks(1000);
                    gpio_wr(P(0,11), 0);
                    delay_ticks(5000);
                    gpio_wr(P(1,9), 1);
                    gpio_wr(P(1,19), 1);
                    gpio_wr(P(1,5), 1);
                    gpio_wr(P(1,31), 1);
                    gpio_wr(P(2,17), 1);
                    gpio_wr(P(0,23), 0);
                    gpio_wr(P(0,8), 1);
                    for (;;) { }                  /* 0x8ae8 */
                }
                else if (ir == 0x15) {
                    led_mode(0x17);
                    gpio_wr(P(2,2), 1);
                    gpio_wr(P(2,8), 1);
                    gpio_wr(P(2,10), 1);
                    gpio_wr(P(2,9), 1);
                    gpio_wr(P(1,26), 1);
                    gpio_wr(P(0,16), 0);
                    for (;;) {                    /* 0x8b30: two presses of the P2_15 key */
                        if (stage == 0) {
                            if (gpio_rd(P(2,15)) == 0) {
                                gpio_wr(P(2,9), 0);
                                stage = 1;
                                gpio_wr(P(1,21), 1);
                                while (gpio_rd(P(2,15)) == 0) {
                                    delay_ticks(500);
                                }
                                delay_ticks(5000);
                            }
                        } else {
                            if (gpio_rd(P(2,15)) == 0) {
                                led_mode(0x17);
                                gpio_wr(P(1,26), 0);
                                gpio_wr(P(1,21), 0);
                                for (;;) { }      /* 0x8b9a */
                            }
                        }
                    }
                }
                else if (ir == 0x16) {
                    led_mode(0x17);
                    gpio_wr(P(2,8), 1);
                    gpio_sel_p2_21_20_n(3);
                    delay_ticks(6000);
                    gpio_wr(P(1,25), 1);
                    delay_ticks(7000);
                    gpio_wr(P(1,31), 1);
                    gpio_wr(P(1,19), 0);
                    gpio_wr(P(0,8), 0);
                    gpio_sel_p1_23_22(0);
                    for (;;) { }                  /* 0x8bf2 */
                }
                else if (ir == 0x10) {
                    factory_test10_active = 1;
                    led_mode(0x17);
                    gpio_wr(P(2,8), 1);
                    gpio_sel_p2_21_20_n(3);
                    delay_ticks(6000);
                    gpio_wr(P(1,25), 1);
                    delay_ticks(7000);
                    power_set_flag_02e6(1);
                    gpio_wr(P(1,31), 1);
                    gpio_wr(P(1,19), 0);
                    gpio_wr(P(0,8), 0);
                    gpio_wr(P(1,17), 0);
                    gpio_wr(P(0,21), 1);
                    gpio_sel_p1_23_22(0);
                    power_on();
                    /* falls back to the key-held loop */
                }
                else if (ir == 0x14) {
                    led_mode(0x17);
                    gpio_wr(P(2,8), 1);
                    gpio_wr(P(2,2), 1);
                    gpio_sel_p2_21_20_n(3);
                    delay_ticks(6000);
                    gpio_wr(P(1,25), 1);
                    delay_ticks(7000);
                    gpio_wr(P(1,31), 1);
                    gpio_wr(P(1,19), 0);
                    gpio_wr(P(0,8), 0);
                    gpio_sel_p1_23_22(1);
                    gpio_wr(P(2,13), 1);
                    for (;;) { }                  /* 0x8cd2 */
                }
                else if (ir == 0x13) {
                    led_mode(0x17);
                    gpio_wr(P(2,8), 1);
                    gpio_wr(P(2,9), 1);
                    gpio_wr(P(0,6), 1);
                    gpio_sel_p1_13_12(1);
                    for (;;) { }                  /* 0x8d24 */
                }
                else if (ir == 3) {
                    factory_test3_active = 1;
                    led_mode(0x17);
                    gpio_wr(P(2,8), 1);
                    gpio_wr(P(2,2), 1);
                    gpio_wr(P(2,9), 1);
                    power_set_flag_02e5(1);
                    gpio_sel_p2_21_20_n(2);
                    delay_ticks(6000);
                    gpio_wr(P(1,25), 1);
                    delay_ticks(7000);
                    gpio_wr(P(2,17), 0);
                    gpio_wr(P(1,17), 1);
                    delay_ticks(30000);
                    gpio_wr(P(1,10), 1);
                    power_on();
                }
                else if (ir == 8) {               /* factory mode := 1 */
                    eeprom_write_byte(0x3e06, 1);
                    factory_mode = 1;
                    for (i = 0; i < 3; i++) {
                        gpio_wr(P(2,8), 1);
                        gpio_wr(P(2,2), 1);
                        gpio_wr(P(2,10), 1);
                        gpio_wr(P(2,9), 1);
                        delay_ticks(5000);
                        led_mode(0x17);
                        delay_ticks(5000);
                    }
                    power_on();
                }
                else if (ir == 0xb) {             /* factory mode := 0 */
                    eeprom_write_byte(0x3e06, 0);
                    factory_mode = 0;
                    for (i = 0; i < 1; i++) {
                        gpio_wr(P(2,8), 1);
                        gpio_wr(P(2,2), 1);
                        gpio_wr(P(2,10), 1);
                        gpio_wr(P(2,9), 1);
                        delay_ticks(5000);
                        led_mode(0x17);
                        delay_ticks(5000);
                    }
                    power_on();
                }
                /* any other code: nothing, re-test the key */
            }
            wake = 0;                             /* 0x8e68 */
        }

        /* ---- normal standby handling (0x8e6a) ---- */
        if (standby_settled() == 0) {
            continue;                             /* note: no wfi on this path */
        }

        wake = key_take_power_event();                         /* power-key event */
        if (wake != 0 && power_get_flag_02f7() == 1) {
            wake = 0;                             /* key locked out */
        }

        if (ir_idle_flag(1) != 0) {
            ir = ir_take_code();
            ir_aux = ir_take_frame_done();                   /* second IR field (address / custom code ?) */
            if (power_get_flag_02f2() == 1 && ir_aux != 0 && power_get_flag_02f1() != ir_aux) {
                ir = 0;                           /* does not match the configured value: ignore */
            }
        }

        uart = uart_wake_token_take();

        if (wake == 1) {
            power_set_wake_reason(2);                           /* wake reason 2: key */
        } else if (ir == 2) {
            wake = 1;
            power_set_wake_reason(3);                           /* wake reason 3: IR power */
            ir = 0;
        } else if (uart == 0) {
            wake = 1;
            power_set_wake_reason(4);                           /* wake reason 4: UART */
        }

        if (eeprom_ac_ok() == 1 && setting_wol_enabled() == 1 && phy_pme_fell() == 1) {
            wake = 1;
            power_set_wake_reason(7);                           /* wake reason 7: WoL magic packet */
        }

        if (wake == 1) {
            power_on();
            wake = 0;
            ir_take_code();                       /* discard */
        }

        __wfi();                                  /* 0x8f14 */
    }
}

/* ------------------------------------------------------------------ */
/* Bit-banged MDIO (IEEE 802.3 clause 22)                              */
/*                                                                     */
/* Every gpio_wr / gpio_dir below is followed in the binary by five    */
/* NOPs (the only bit delay).  They are folded into these macros:      */
/* ------------------------------------------------------------------ */
#define MDIO_DATA           P(1,30)
#define MDIO_CLK            P(1,29)
#define MDIO_OUT(v)         do { gpio_wr(MDIO_DATA, (v)); NOP5(); } while (0)
#define MDIO_CLK_PULSE()    do { gpio_wr(MDIO_CLK, 1); NOP5(); \
                                 gpio_wr(MDIO_CLK, 0); NOP5(); } while (0)

/* 0x8fcc  callers: 1 (mdio_read) */
/* Opcode "10" = read. */
void mdio_op_read(void)
{
    MDIO_OUT(1);  MDIO_CLK_PULSE();
    MDIO_OUT(0);  MDIO_CLK_PULSE();
}

/* 0x9048  callers: 1 (mdio_write) */
/* Opcode "01" = write. */
void mdio_op_write(void)
{
    MDIO_OUT(0);  MDIO_CLK_PULSE();
    MDIO_OUT(1);  MDIO_CLK_PULSE();
}

/* 0x90c4  callers: 4 */
/* Shift out a 5-bit field (PHY address or register number), MSB first.
 * Loop counter is the RAM byte mdio_bit_count. */
void mdio_out5(u8 v)
{
    for (mdio_bit_count = 0; mdio_bit_count < 5; mdio_bit_count++) {
        MDIO_OUT((v & 0x10) >> 4);
        MDIO_CLK_PULSE();
        v = (u8)(v << 1);
    }
}

/* 0x912c  callers: 2 */
/* One trailing clock with the data line driven high (bus idle). */
void mdio_idle_bit(void)
{
    MDIO_OUT(1);  MDIO_CLK_PULSE();
}

/* 0x916c  callers: 5 (all in phy_arm_wol) */
/* Clause-22 read.  Returns the 16-bit register value (accumulated in the
 * RAM word mdio_shift_reg).  The MCU drives the "10" turnaround itself before
 * releasing the data line. */
u32 mdio_read(u8 phy, u8 reg)
{
    gpio_dir(MDIO_DATA, 1);  NOP5();
    mdio_preamble();                                    /* preamble: 32 clocks */
    mdio_start();                                    /* start "01" */
    mdio_op_read();                                    /* opcode read "10" */
    mdio_out5(phy);
    mdio_out5(reg);
    mdio_turnaround();                                    /* turnaround "10" */
    gpio_dir(MDIO_DATA, 0);  NOP5();              /* release data line */

    mdio_shift_reg = 0;
    for (mdio_bit_count = 0; mdio_bit_count < 0x10; mdio_bit_count++) {
        mdio_shift_reg = mdio_shift_reg << 1;
        mdio_shift_reg = ((gpio_rd(MDIO_DATA) != 0) ? 1 : 0) | mdio_shift_reg;   /* sampled before the clock pulse */
        MDIO_CLK_PULSE();
    }

    gpio_dir(MDIO_DATA, 1);  NOP5();
    mdio_idle_bit();
    return mdio_shift_reg;
}

/* 0x9248  callers: 2 */
/* Start of frame "01". */
void mdio_start(void)
{
    MDIO_OUT(0);  MDIO_CLK_PULSE();
    MDIO_OUT(1);  MDIO_CLK_PULSE();
}

/* 0x92c4  callers: 2 */
/* Turnaround "10". */
void mdio_turnaround(void)
{
    MDIO_OUT(1);  MDIO_CLK_PULSE();
    MDIO_OUT(0);  MDIO_CLK_PULSE();
}

/* 0x9340  callers: 2 */
/* Preamble: 32 clock pulses.  The data line is NOT written here; it has
 * just been switched to output by the caller and keeps whatever level
 * the output latch holds. */
void mdio_preamble(void)
{
    mdio_bit_count = 0x20;
    while (mdio_bit_count-- != 0) {                       /* test old value, store decremented */
        MDIO_CLK_PULSE();
    }
}

/* 0x9388  callers: 13 (all in phy_arm_wol) */
/* Clause-22 write of a 16-bit value, MSB first. */
void mdio_write(u8 phy, u8 reg, u32 val)
{
    gpio_dir(MDIO_DATA, 1);  NOP5();
    mdio_preamble();                                    /* preamble */
    mdio_start();                                    /* start "01" */
    mdio_op_write();                                    /* opcode write "01" */
    mdio_out5(phy);
    mdio_out5(reg);
    mdio_turnaround();                                    /* turnaround "10" */

    for (mdio_bit_count = 0; mdio_bit_count < 0x10; mdio_bit_count++) {
        MDIO_OUT((val & 0x8000) >> 15);
        MDIO_CLK_PULSE();
        val = val << 1;
    }
    mdio_idle_bit();
}

/* ------------------------------------------------------------------ */

/* 0x9428  callers: 1 (power_off, 0x755e) */
/* If enable is non-zero: repeat main's first init call, then
 * power_sleep_wfi(0, 0xd8).  (Purpose of both callees not known from this range.) */
void fn_9428(u32 enable)
{
    if (enable != 0) {
        clock_init(1, 1, 3, 1);
        power_sleep_wfi(0, 0xd8);
    }
}

/* 0x944c  callers: 6 */
/* Table-driven CRC-16, the Modbus reference implementation: both halves
 * start at 0xff; tables rom_9488 ("auchCRCHi") and rom_9588 ("auchCRCLo").
 * Returns (hi << 8) | lo. */
u16 crc16_modbus(const u8 *p, u8 len)
{
    u8 hi = 0xff;
    u8 lo = 0xff;
    u8 idx;

    while (len-- != 0) {
        idx = *p++ ^ hi;
        hi  = rom_9488[idx] ^ lo;
        lo  = rom_9588[idx];
    }
    return (u16)((hi << 8) + lo);
}

/* ==================================================================
 * Constant data 0x9480 .. 0x9a00
 * ==================================================================
 *
 * 0x9480  literal pool of crc16_modbus (0x9488, 0x9588).
 *
 * 0x9488  rom_9488[256]  CRC-16/Modbus high-byte table
 *                        (00 c1 81 40 01 c0 80 41 ...).
 * 0x9588  rom_9588[256]  CRC-16/Modbus low-byte table
 *                        (00 c0 c1 01 c3 03 02 c2 ...).
 *
 * 0x9688  rom_9688[16]   be ef 10 06 00 00 00 33 33 02 00 01 ff 00 00 00
 *                        frame template copied to the stack by the function
 *                        containing 0x7a5c (outside this range).
 * 0x9698  rom_9698[16]   identical 16 bytes; copied by the function
 *                        containing 0x79aa.
 * 0x96a8  rom_96a8[12]   2a 20 01 01 01 03 50 00 00 00 00 00
 *                        template copied by uart1_send_2a20_frame.
 *
 * 0x96b4  rom_96b4[80]   pin setup table, 8 bytes per pin, P0_0..P0_23,
 *                        P1_0..P1_31, P2_0..P2_23, walked twice by gpio_init_table:
 *     u16 id      (port<<8)|pin
 *     u8  func    passed as 2nd argument to gpio_as_input(id, func) in pass 1
 *     u8  dir     pass 2: 1 -> gpio_port_write(port, pin, mode & 1)  (output, initial level)
 *                         0 -> gpio_dir_clear(port, pin); iocon_cfg(port, pin, (mode >> 2) & 3)  (input)
 *                         anything else -> pin left alone in pass 2
 *     u8  mode    see above
 *     u8  pad[3]  zero
 *   Entries as (func, dir, mode), hex:
 *     P0_0  0,40,9   P0_1  0,1,b   P0_2  0,1,4   P0_3  0,1,8
 *     P0_4  1,2,a    P0_5  1,2,a   P0_6  0,1,8   P0_7  0,1,a
 *     P0_8  0,1,a    P0_9  0,0,4   P0_10 0,0,8   P0_11 1,1,a
 *     P0_12 1,0,4    P0_13 1,1,a   P0_14 1,1,a   P0_15 0,1,b
 *     P0_16 0,1,8    P0_17 0,1,b   P0_18 0,0,6   P0_19 0,0,6
 *     P0_20 0,0,7    P0_21 0,1,a   P0_22 0,1,a   P0_23 0,1,8
 *     P1_0  3,3,b    P1_1  0,3b,b  P1_2  0,1,8   P1_3  0,1,a
 *     P1_4  0,1,a    P1_5  0,1,a   P1_6  2,3,b   P1_7  0,1,8
 *     P1_8  0,1,8    P1_9  0,1,8   P1_10 0,1,a   P1_11 1,2,a
 *     P1_12 0,1,a    P1_13 0,1,a   P1_14 1,2,a   P1_15 0,1,a
 *     P1_16 0,0,9    P1_17 0,1,a   P1_18 0,1,8   P1_19 0,1,a
 *     P1_20 0,1,8    P1_21 0,1,8   P1_22 0,1,8   P1_23 0,1,a
 *     P1_24 0,1,a    P1_25 0,1,a   P1_26 0,1,a   P1_27 0,0,2
 *     P1_28 0,0,4    P1_29 0,1,a   P1_30 0,0,a   P1_31 0,1,a
 *     P2_0  1,0,6    P2_1  1,1,a   P2_2  3,1,a   P2_3  1,3,b
 *     P2_4  1,3,b    P2_5  0,0,4   P2_6  0,1,a   P2_7  0,1,a
 *     P2_8  0,1,a    P2_9  0,1,a   P2_10 0,1,a   P2_11 1,3,a
 *     P2_12 1,3,a    P2_13 0,1,a   P2_14 0,0,5   P2_15 0,0,9
 *     P2_16 0,0,9    P2_17 0,1,8   P2_18 0,1,9   P2_19 0,1,8
 *     P2_20 0,1,b    P2_21 0,1,b   P2_22 0,0,6   P2_23 0,1,a
 *   Only P0_1, P0_15, P0_17, P2_18, P2_20, P2_21 are outputs with initial
 *   level 1 (mode bit 0).  The dir values 2, 3, 0x3b, 0x40 are not
 *   interpreted by gpio_init_table.
 *
 * 0x9934  rom_9934[6]    EEPROM geometry, 6 bytes per type, indexed by
 *                        eeprom_type_idx (used by eeprom_addr_split):
 *     u8 two_byte_addr, u8 pad, u16 block_size, u8 n_blocks, u8 pad
 *     [0] 0, 0x100, 1     256 B   (24C02-class)
 *     [1] 0, 0x100, 2     512 B
 *     [2] 0, 0x100, 4     1 KB
 *     [3] 0, 0x100, 8     2 KB
 *     [4] 1, 0x100, 0x10  4 KB    (two address bytes)
 *     [5] 1, 0x100, 0x40  16 KB   (two address bytes; the type in use,
 *                                  settings pointer table at 0x3ff6)
 *
 * 0x9958  rom_9958  "#00500 0" + 4 x NUL   (12-byte slots)
 * 0x9964  rom_9964  "#00500 1" + 4 x NUL
 * 0x9970  rom_9970  "#00501 1" + 4 x NUL
 *                   USART2 ASCII commands compared by uart2_task.
 *
 * 0x997c  rom_997c  Keil scatter-load region table, 2 entries of
 *                   { u32 src, u32 dst, u32 len, u32 handler }:
 *     { 0x0000999c, 0x10000100, 0x000001fc, 0x000023d6 }  .data, decompressor
 *     { 0x000099e8, 0x100002fc, 0x00001ac4, 0x00002104 }  .bss, zero-fill
 *
 * 0x999c  rom_999c  compressed .data image, 0x4c bytes (0x999c..0x99e7),
 *                   expands to 0x1fc bytes at 0x10000100.  Recognisable in
 *                   the packed stream: the three pointers 0x9958 / 0x9964 /
 *                   0x9970 (command string table), the default EEPROM device
 *                   address 0xac, an ascending u16 table
 *                   (.., 0x5a, 0x78, 0x97, 0xb5, 0xd4, 0xf3, 0x111, 0x130,
 *                   0x14e, 0x16e -- cumulative days per month ?), and the
 *                   text "0123456789XYZ".  Not unpacked here; see
 *                   tools/nxp_initdata.py.
 *
 * 0x99e8 .. 0x99ff  0xff padding (erased flash) to the end of the image.
 */
