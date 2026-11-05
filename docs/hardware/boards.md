# Boards

Hardware facts for the three supported boards. Every board is an ESP32-S3 with an RGB parallel panel and a GT911 touch controller. Items marked **confirm** are not stated in Waveshare's documentation and have to be checked on the bench or in the schematic before that profile is written.

## square4: Waveshare ESP32-S3 4" square (480×480)

| | |
|---|---|
| Panel | ST7701, 480×480 IPS. Vendor init over 3-wire SPI, then RGB565 parallel |
| Touch | GT911 on I2C |
| Expander | TCA9554 at 0x20 on hardware v3, CH32V003 at 0x24 on v4. Probe both at boot |
| SD | SDMMC |
| Buzzer | Yes |
| Extras | RTC, IMU (unused in v1) |

Both expander versions are in the field, so the profile probes rather than assumes.

**Bench unit (2026-11-02), probed with `diag/probe.defaults`:** 16 MB flash, 8 MB octal PSRAM. Every I2C device is on one bus, SDA GPIO15 / SCL GPIO7: `0x24` (expander, the v4 kind, not TCA9554), `0x51` (PCF85063 RTC), `0x5D` (GT911). Nothing answers on GPIO8/GPIO9. Waveshare's wiki notes that newer revisions put I2C0 and I2C1 on one bus. The 0x24 expander shares its address with the 7B's, which suggests the same chip and protocol. Confirm the register protocol from Waveshare's v4 or 7B ESP-IDF demo before writing the driver.

From Waveshare's wiki for this board (the pin table is flattened, so the grouping is partly inferred):

| Signal | Pin |
|---|---|
| RGB | HSYNC 38, VSYNC 39, DE 40, PCLK 41. R1 46, R2 3, R3 8, R4 18, R5 17. G0 14, G1 13, G2 12, G3 11, G4 10, G5 9. B1 5, B2 45, B3 48, B4 47, B5 21 |
| ST7701 3-wire SPI | SDA GPIO1, SCL GPIO2, CS GPIO42. Reset on expander |
| Touch | GT911 0x5D, INT GPIO16, reset on expander |
| SD | GPIO1/2/4 (shared with the ST7701 init lines), CS on expander. The wiki shows SPI and SDMMC naming; **confirm** which bus |
| CAN | RX GPIO0, TX GPIO6 (TJA1051) |
| RS485 | RX GPIO43, TX GPIO44 |
| PCLK | 21 MHz in Waveshare's LVGL benchmark |

**V4.0 facts (2026-11-02), from Waveshare's GitHub repo `waveshareteam/ESP32-S3-Touch-LCD-4` (Apache-2.0): its `esp32_s3_touch_lcd_4` BSP v3.0.0, the `custom_io_expander_ch32v003` component, and the V4.0 schematic:**

- The 0x24 chip is a CH32V003 running Waveshare firmware. Registers: 0x02 direction (bit 1 = output), 0x03 output, 0x04 input, 0x05 backlight PWM (inverted: 0 is full, 255 is off), 0x06 battery ADC (10-bit, little-endian), 0x07 RTC interrupt. Each write is `[reg][value]`.
- Expander pins: EXIO1 TP_RST, EXIO2 TP_INT (schematic), EXIO3 LCD_RST, EXIO5 SYS_EN (power), EXIO6 buzzer, EXIO7 RTC_INT. The backlight is the PWM register, not a pin. There is no SD chip select; SD is SDMMC 1-bit on CLK GPIO2, CMD GPIO1, D0 GPIO4.
- Panel: `ST7701_480_480_PANEL_60HZ_RGB_TIMING()` from `espressif/esp_lcd_st7701`, a 16-bit bus, and the BSP's vendor init sequence (copied in `board_square4.c`). Data D0–D15 = GPIO 5, 45, 48, 47, 21, 14, 13, 12, 11, 10, 9, 46, 3, 8, 18, 17. The 3-wire SPI is CS 42, SCL 2, SDA 1. Set `auto_del_panel_io` so GPIO1/2 are released for SD; it cannot be combined with `mirror_by_cmd`.
- **Bench (2026-11-02):** color bars and the shell display correctly, SD mounts and passes the self-check, and the buzzer chirps at power-up until the firmware drives it low. **Touch fix:** power up as Waveshare's WS_CH32_IO library does: all eight helper pins are outputs, all low for 200 ms, then SYS_EN, LCD_RST, and TP_RST high. That holds EXIO2 (GT911 INT) low, and the GT911 starts at 0x5D with config version 79. With EXIO2 floating at reset, it came up at 0x14 with config version 0 and never reported touches. EXIO4 is SD D3/CS and is kept high so the card stays in SD mode.

The wiki's expander pin list (EXIO0 TP_RST, EXIO1 BL_EN, EXIO2 LCD_RST, EXIO3 SD_CS, EXIO4 BLC, EXIO5 BEE_EN, EXIO6 RTC_INT, EXIO7 DO1) is for the TCA9554 revision. **confirm** the v4 mapping. The 4" was historically hard to bring up. Keep a build flag for RGB color-bar and I2C-probe diagnostics.

## lcd7: Waveshare ESP32-S3-Touch-LCD-7 (800×480)

| | |
|---|---|
| Panel | Generic RGB, 800×480 IPS. No vendor init sequence |
| Touch | GT911 at 0x5D. SDA GPIO8, SCL GPIO9, IRQ GPIO4. Reset on expander EXIO1 |
| Expander | CH422G. EXIO1 touch reset, EXIO2 backlight enable, EXIO4 SD CS (active low), EXIO5 USB/CAN select |
| SD | SPI. MOSI GPIO11, SCK GPIO12, MISO GPIO13. CS on EXIO4 |
| Backlight | On/off through EXIO2. **confirm** whether brightness can be dimmed |
| Buzzer, RTC, IMU | None |
| CAN | TX GPIO20, RX GPIO19. Shares the native USB pair. EXIO5 high selects CAN and disables native USB |
| RS485 | TX GPIO15, RX GPIO16 |
| UART | Separate CH343 Type-C port. The UART switch must be on UART1 for it to pass bytes |
| Benchmark PCLK | 21 MHz (Waveshare LVGL demo) |

**Bench unit (2026-11-02), probed with `diag/probe.defaults`:** GPIO8/GPIO9 answers at 0x20–0x27 and 0x30–0x3F (CH422G, which uses those addresses as registers) and 0x5D (GT911). The CH343 UART enumerates as USB 1a86:55d3.

**Bring-up (2026-11-02):** working with 16 MHz PCLK, HSYNC/VSYNC pulse 4, back and front porch 8, PCLK active on the falling edge, 10-line bounce buffer, and two PSRAM framebuffers. Source: Espressif's ESP32_Display_Panel board definition for this board (Apache-2.0). Expander pins: EXIO1 touch reset, EXIO2 backlight, EXIO3 LCD reset, EXIO4 SD CS (held low; the card is the only SPI device), EXIO5 low for native USB. RGB data D0–D15 are B3–B7, G2–G7, R3–R7 = GPIO 14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40. The panel controller is an ST7262, which needs no init sequence. With bounce buffers, wait for the driver's `on_frame_buf_complete` before drawing into the old buffer. `on_vsync` comes too late.

**Resolved (2026-11-02):** brief corruption during swipes on lcd7 and square4, then vertical jitter on lcd7 once Wi-Fi came up, had one cause: PSRAM bandwidth. The panel's bounce buffers are refilled from the PSRAM framebuffer, and a full-page redraw or Wi-Fi traffic competes for the same bus. Fixed with a 64 KB data cache and 64-byte cache lines (`sdkconfig.defaults`) plus 20-line bounce buffers on every board. If it comes back, try a lower PCLK next.

The bench unit was probed with `esptool.py flash_id`: flash manufacturer `0x46`, device `4018` (16 MB), 8 MB octal PSRAM. Waveshare's spec table says 8 MB flash, which contradicts the N16R8 module name. Trust the probe. An image built for 16 MB flash crashes on a chip that probes as 4 MB (`spi_flash: Detected size(4096k) ... header(16384k)`). Run `flash_id` on every new unit before the first image.

## lcd7b: Waveshare ESP32-S3-Touch-LCD-7B (1024×600)

| | |
|---|---|
| Panel | RGB, 1024×600 IPS. **confirm** driver chip and whether an init sequence is needed (the wiki FAQ mentions ST7701, unverified) |
| Timings | **confirm** porch values and pixel clock from the schematic or Waveshare's IDF demo. The LVGL benchmark note gives 30 MHz PCLK |
| RGB pins | GPIO0 G3, GPIO1 R3, GPIO2 R4, GPIO3 VSYNC, GPIO5 DE, GPIO7 PCLK, GPIO10 B7, GPIO14 B3, GPIO17 B6, GPIO18 B5, GPIO21 G7, GPIO38 B4, GPIO39 G2, GPIO40 R7, GPIO41 R6, GPIO42 R5, GPIO45 G4, GPIO46 HSYNC, GPIO47 G6, GPIO48 G5 |
| Touch | GT911, probably 0x5D. SDA GPIO8, SCL GPIO9, IRQ GPIO4. Reset on EXIO1 |
| Expander | Not named by Waveshare ("IO EXTENSION"). The I2C table shows 0x24 in use, the same address as the 4" v4's CH32V003. **confirm** chip and register protocol. EXIO1 touch reset, EXIO2 backlight enable, EXIO4 SD CS (active low), EXIO5 USB/CAN select (high = CAN), EXIO6 LCD VCOM enable (new on 7B) |
| SD | SPI. MOSI GPIO11, SCK GPIO12, MISO GPIO13. CS on EXIO4 |
| Backlight | Enable on EXIO2 through a boost chip. Waveshare says brightness is adjustable. **confirm** the mechanism |
| Memory | 16 MB flash, 8 MB octal PSRAM |
| CAN | TX GPIO20, RX GPIO19, TJA1051, DIP-switch termination. Shares USB through EXIO5 |
| RS485 | TX GPIO15, RX GPIO16 |
| Buzzer, RTC, IMU | Not listed |
| Battery | PH2.0 Li-ion socket with charger. **confirm** the ADC pin for battery voltage |

**Resolved from Waveshare's ESP-IDF demo** (`waveshareteam/ESP32-S3-Touch-LCD-7B`, Apache-2.0, `examples/ESP-IDF/06_LCD`):

- Panel ST7262 (the wiki's datasheet link), no init sequence. 21 MHz PCLK. HSYNC pulse 162, back porch 152, front porch 48. VSYNC pulse 45, back porch 13, front porch 3. PCLK active on the falling edge. 10-line bounce buffer, two framebuffers.
- The I/O helper at 0x24 uses the same register protocol as the 4" CH32V003 (0x02 mode, 0x03 output, 0x04 input, 0x05 PWM, 0x06 ADC). The demo makes every pin an output and starts them all high. EXIO1 touch reset, EXIO2 backlight enable, EXIO3 LCD reset, EXIO4 SD CS, EXIO5 USB(0)/CAN(1), EXIO6 VCOM enable.
- Backlight: EXIO2 is on/off. PWM 0x05 dims, inverted, and the demo caps it at 97%.
- RGB pins are identical to the 800×480 board.

`board_lcd7b.c` was written from these and has run on the bench since 2026-11-03: bring-up, the swipe tearing fix below, and the internal RAM fix (see Internal RAM).

**7B (2026-11-03):** the same tearing appeared on the 7B at 30 MHz PCLK; 21 MHz (about 23 fps with these porches) removed it with no visible flicker.

Waveshare 7B wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7B. Schematic: https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-7B/ESP32-S3-Touch-LCD-7B-Schematic.pdf

## Shared notes

- All three use the `esp_lcd` RGB panel driver with PSRAM framebuffers and bounce buffers. On the 7" boards this is what prevents drift and tearing while Wi-Fi is busy.
- On the 7" boards, enabling CAN for NMEA 2000 disables native USB. Flashing and logs then go through the UART port. Ship NMEA 2000 as a config choice, never as a default.
- `ARDUINO_USB_CDC_ON_BOOT`-style console routing is a per-board setting, not a shared default. Each 7" board has a separate UART Type-C port.

## Internal RAM

Internal RAM is the scarce resource on these boards, not PSRAM. The RGB panel's two 20-line bounce buffers must be DMA-capable internal RAM: about 80 KB on the 7B (1024 × 20 px × 2 bytes × 2), less on the others. Wi-Fi and every task stack also come from internal RAM. On 2026-11-03 the 7B ran out at boot, and the Signal K task was never created, so the display never connected. Large static buffers now go to PSRAM: tag a zero-initialised static with `PLATFORM_EXT_BSS` (from `platform.h`) unless an ISR or a DMA engine touches it. Check the boot log line `RAM after start: internal free …, largest block …; PSRAM free …` after a change that adds memory. A task that cannot be created logs its name, stack size, and the internal free and largest block. The `status` serial command reports internal and PSRAM free; read it after a fresh boot has been connected for about 45 s. Measured 2026-11-04 (settled, connected to Signal K): square4 74 KB internal free, lcd7 38 KB (`main` before the Glance page: 44 KB; the Glance page's LVGL objects come from malloc, so internal RAM). The history backfill logs `signalk: history <path>: <rows> rows (internal heap free …, lowest since boot …)` per path; lcd7's lowest since boot was about 30 KB. Signal K response bodies are allocated in PSRAM (`transport_esp.c`), because a 12 hour history reply is about 25 KB and its buffer doubles as it grows, which in internal RAM would take about 24 KB at once on lcd7. That peak has not been measured with a full-size reply: the bench server returned empty history. With the Anchor role (measured 2026-11-05, same conditions, 45 s after boot, no anchor set): square4 71 KB internal free, lcd7 36 KB (lowest since boot 28 KB), lcd7b 31 KB (lowest 24 KB). That is 2 to 3 KB less than before the role, which is its LVGL objects, the 720-point track and the command queue. The Signal K task's least free stack was about 4.4 KB on all three before any command was sent; send a drop from each board and read `status` again, since a command send runs an HTTP request on that task.

`PLATFORM_EXT_BSS` needs `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`, which is in `sdkconfig.defaults`. Defaults only apply when the sdkconfig is generated, so a build directory created before 2026-11-03 keeps the option off and would boot into the same failure. `platform.h` stops such a build with an `#error`; run `idf.py -B build/<board> fullclean`, then build again with `-D BOARD=<board>`.
