**Pre-release. Not fit for use on a boat.** Do not rely on this software for navigation or to protect your engine, batteries or boat. It has been tested in a simulator and on bench boards only, and its NMEA 2000 message layouts have not been checked against real devices.

Install from the browser at <https://ryanillman.github.io/saloon-watchkeeper/> (desktop Chrome or Edge), or flash the image for your board at offset `0x0`. The files are named `saloon-watchkeeper-<board>-<tag>.bin`:

| Board | Display |
|---|---|
| `square4` | ESP32-S3-Touch-LCD-4 (4", 480x480) |
| `lcd7` | ESP32-S3-Touch-LCD-7 (7", 800x480) |
| `lcd7b` | ESP32-S3-Touch-LCD-7B (7", 1024x600) |

Check a download against `SHA256SUMS`. Settings live on the SD card and survive an update.
