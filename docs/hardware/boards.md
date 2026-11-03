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

Both expander versions are in the field, so the profile probes rather than assumes. The 4" was historically hard to bring up. Keep a build flag for RGB color-bar and I2C-probe diagnostics.

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

Waveshare 7B wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7B. Schematic: https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-7B/ESP32-S3-Touch-LCD-7B-Schematic.pdf

## Shared notes

- All three use the `esp_lcd` RGB panel driver with PSRAM framebuffers and bounce buffers. On the 7" boards this is what prevents drift and tearing while Wi-Fi is busy.
- On the 7" boards, enabling CAN for NMEA 2000 disables native USB. Flashing and logs then go through the UART port. Ship NMEA 2000 as a config choice, never as a default.
- `ARDUINO_USB_CDC_ON_BOOT`-style console routing is a per-board setting, not a shared default. Each 7" board has a separate UART Type-C port.
