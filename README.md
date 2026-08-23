# ESP32-S3 Wi-Fi print server for the Zebra ZP450

Turn an ESP32-S3 into a small raw TCP/IP print server for a USB-only Zebra
ZP450. Windows prints to the ESP32 over Wi-Fi on TCP port 9100; the ESP32
forwards the resulting ZPL/EPL bytes unchanged to the printer's USB bulk OUT
endpoint.

This is a byte bridge, not a rasterizer. Label layout and printer-language
generation remain the responsibility of the Windows Zebra driver or the client
application.

## Confirmed setup

- Lonely Binary ESP32-S3 DevKitC-1 N16R8, dual USB-C
- ESP-IDF 5.5.5
- Zebra ZP450, 203 dpi UPS model
- Confirmed onboard USB-OTG/VBUS solder jumper bridged
- Direct USB-C-to-USB-B data cable between the ESP32 and printer
- Windows 11 using the UPS Zebra driver and a Standard TCP/IP Port

Other ESP32-S3/S2 boards and USB Printer Class devices may work, but have not
been tested here.

## Features

- USB Printer Class discovery; no hard-coded Zebra VID/PID
- Standard raw printing on TCP port 9100
- `POST /print` endpoint for raw ZPL/EPL jobs
- `GET /health` endpoint for printer and queue status
- `.local` mDNS hostname
- External USB hub support
- Onboard RGB status LED support for the Lonely Binary board
- Bounded, streaming 4 KiB print queue; no whole-job allocation

## Hardware and wiring

The Lonely Binary board has two USB-C connectors:

- **UART:** power, flashing, and serial monitor
- **USB:** native ESP32-S3 USB-OTG data port used as the printer host

Working topology:

```text
5 V USB supply/computer
        |
        v
ESP32 UART USB-C

ESP32 USB/OTG USB-C
        |
        v
Zebra ZP450 USB-B + its normal power supply
```

The ZP450 is self-powered, but its USB interface still expects the host-side
VBUS signal. On the tested Lonely Binary board, powering through UART does not
put VBUS on the native USB port until the onboard jumper is bridged.

The confirmed jumper is the small two-pad footprint on the back of the board
beside the port labeled **USB**. It bypasses the USB port's SS14 isolation diode,
allowing UART-supplied 5 V to reach native-USB VBUS. Verify the pads with a
multimeter before soldering: unpowered, each pad should have continuity to one
end of the SS14 diode nearest the USB port. With UART power applied and the
jumper open, the board-side pad measures about 5 V while the USB-side pad
measures 0 V.

Use a properly reflowed solder bridge; an intermittent bridge can cause erratic
USB enumeration. A powered USB hub remains a valid no-solder alternative.

After bridging the jumper, do not connect UART to one computer/power source
while connecting the native USB port to another powered USB host. The jumper
ties their 5 V rails together. UART power plus the self-powered ZP450 is the
confirmed intended arrangement.

On an ESP32-S3, native USB uses GPIO 19 for D- and GPIO 20 for D+. A board's
separate UART/COM connector is not the USB host data port.

### Status LED

The Lonely Binary onboard WS2812 is on GPIO 48. GPIO and brightness are
configurable in `menuconfig`.

| Color | State |
| --- | --- |
| Purple | Connecting or reconnecting to Wi-Fi |
| Blue | Network ready; USB printer not detected |
| Green | Printer detected and ready |
| Cyan | Sending a job over USB |
| Red | USB transfer/print failure |

## Build and flash

Install ESP-IDF 5.3 or newer. ESP-IDF 5.5.5 was used for the confirmed build.
The VS Code extension is optional; all commands below run in an ESP-IDF Command
Prompt or PowerShell.

If using an ordinary Windows Command Prompt, initialize ESP-IDF first:

```cmd
C:\Espressif\frameworks\esp-idf-v5.5.5\export.bat
```

From the project directory:

```cmd
idf.py set-target esp32s3
idf.py menuconfig
```

Open **ZP450 print server** and configure:

- Wi-Fi SSID
- Wi-Fi password
- Hostname, default `zp450`
- RGB LED GPIO/brightness if needed

Save, connect the board's **UART** USB-C connector, then build and flash:

```cmd
idf.py -p COM5 flash monitor
```

Replace `COM5` with the board's actual serial port. Exit the monitor with
`Ctrl+]`.

The project declares the managed `espressif/mdns` and `espressif/led_strip`
components. The initial build may download them. External hub support is enabled
in `sdkconfig.defaults`.

The included `.gitignore` excludes `sdkconfig` because it contains the configured
Wi-Fi credentials. Commit `sdkconfig.defaults`; also commit `dependencies.lock`
after the first successful build to pin the resolved managed-component versions.

## Network checks

With the ESP32 blue or green, open:

```text
http://zp450.local/health
```

Example response before the USB printer is attached:

```json
{"printer":"offline","queued_chunks":0}
```

Example when ready:

```json
{"printer":"online","queued_chunks":0}
```

`zp450.local` uses mDNS and normally works only within the same subnet. Across
VLANs, use the ESP32's reserved address or a hostname registered by your local
DNS server.

## Direct print tests

### PowerShell: TCP 9100

This is the best end-to-end test before configuring Windows printing:

```powershell
$tcp = [Net.Sockets.TcpClient]::new("zp450.local", 9100)
$data = [Text.Encoding]::ASCII.GetBytes("^XA^FO30,30^A0N,35,35^FDESP32 TEST^FS^XZ")
$tcp.GetStream().Write($data, 0, $data.Length)
$tcp.Close()
```

The LED should flash cyan and the label should print.

### HTTP

```sh
curl --data-binary '^XA^FO30,30^A0N,35,35^FDESP32 HTTP TEST^FS^XZ' \
  http://zp450.local/print
```

### Netcat

```sh
nc zp450.local 9100 < shipping-label.zpl
```

The ZP450 exists with special UPS firmware and in different language workflows.
The server does not translate EPL to ZPL or vice versa. Send the language that
your printer/driver already accepts. Do not flash generic Zebra firmware onto a
UPS ZP450.

## Windows 10/11 installation

### 1. Obtain and extract the UPS driver

Download the Zebra driver from the official [UPS thermal printing support
page](https://www.ups.com/us/en/support/shipping-support/print-shipping-labels/thermal-printing-of-labels).
The downloaded installer is typically named `UPSZebraDriver.exe`.

For a manual IP-printer installation, extract the executable with 7-Zip or a
similar archive tool instead of relying on its USB-oriented installer. Locate:

```text
ZBRN.inf
```

Do not commit the UPS driver executable or extracted driver files to this
repository. They are third-party software; link users to UPS instead.

### 2. Add the printer manually

1. Open **Settings > Bluetooth & devices > Printers & scanners**.
2. Click **Add device**, wait for discovery, then choose **Add manually**.
3. Select **Add a local printer or network printer with manual settings**.
4. Choose **Create a new port** and select **Standard TCP/IP Port**.
5. Enter `zp450.local`, a local-DNS name, or the ESP32's reserved IP address.
6. Uncheck **Query the printer and automatically select the driver**.
7. Choose **Custom > Settings** and configure:
   - Protocol: **Raw**
   - Port number: **9100**
   - SNMP Status Enabled: **off**
8. At the driver selection screen, click **Have Disk**.
9. Browse to the extracted `ZBRN.inf`.
10. Select **Zebra ZP 450-200 dpi** and finish the wizard.

Check the hostname carefully. A typo such as `zp450.locl` still creates the
printer and port but causes every job to fail without reaching the ESP32.

### 3. Verify Windows properties

Open **Printer properties**:

- **Ports**
  - Correct Standard TCP/IP Port selected
  - Raw protocol, TCP 9100
  - SNMP disabled
  - Bidirectional support disabled
- **Advanced**
  - Spool print documents
  - Start printing immediately
  - Print processor: `WinPrint`
  - Default data type: `RAW`

Advanced printing features worked in the confirmed setup and can remain
enabled.

Verify the actual configuration from Administrator PowerShell:

```powershell
$p = Get-Printer | Where-Object DriverName -eq "Zebra  ZP 450-200 dpi"
$p | Format-List Name,DriverName,PortName,PrintProcessor,Datatype,PrinterStatus
Get-PrinterPort -Name $p.PortName | Format-List PrinterHostAddress,Protocol,PortNumber,SNMPEnabled
```

Some releases of the UPS driver contain two spaces in the driver/printer name,
as shown above. Use `Get-Printer` without filters if the command returns no
match.

### Force-clear a stuck Windows queue

From an Administrator Command Prompt:

```cmd
net stop spooler
taskkill /F /IM splwow64.exe
taskkill /F /IM PrintIsolationHost.exe
del /F /Q "%windir%\System32\spool\PRINTERS\*"
net start spooler
```

`Process not found` from either `taskkill` command is harmless. This deletes
queued print jobs only.

## API

### `GET /health`

Returns the USB printer state and number of queued chunks.

### `POST /print`

Queues the request body as a single raw print job. Maximum HTTP request size is
512 KiB by default and is configurable in `menuconfig`.

### TCP `9100`

Streams raw bytes through a bounded queue in 4 KiB chunks. This avoids a large
contiguous allocation and allows Windows to spool jobs of arbitrary size.

## Troubleshooting

### LED remains blue with printer connected

- Confirm the printer cable is attached to the ESP32 port labeled **USB**, not
  **UART**.
- Confirm the cable carries data.
- The printer may be self-powered but still require host VBUS detection.
- On the Lonely Binary board, confirm the USB-side VBUS jumper is properly
  soldered; reflow it if enumeration is intermittent.
- Alternatively, use a powered USB hub.
- External hubs must be enabled with `CONFIG_USB_HOST_HUBS_SUPPORTED=y`.

The following warning is expected from some powered hubs and was harmless in
the confirmed setup:

```text
ENUM: Device has more than 1 configuration
```

### TCP 9100 connects but Windows prints nothing

- Verify the Standard TCP/IP Port hostname for typos.
- Disable SNMP and bidirectional support.
- Confirm the port is Raw TCP 9100, not WSD, IPP, or LPR.
- Run the direct PowerShell TCP test above.

### Windows queue is stuck

- Stop the spooler before resetting/testing the ESP32; Windows may leave a TCP
  connection open.
- Force-clear the queue using the commands above.
- Confirm the current firmware includes streaming TCP support.

### ESP32 reboots with `stack overflow in task raw_9100`

Use the current source. Earlier development code placed the 4 KiB network
buffer on a 4 KiB FreeRTOS task stack. The current version allocates that buffer
from heap and gives the task additional stack headroom.

### Raw label prints as text or does nothing

Confirm whether the specific printer firmware expects ZPL or EPL. The server
passes bytes through unchanged.

## Design notes and limitations

- The code scans active USB descriptors for a class 7 Printer interface and a
  bulk OUT endpoint.
- Only the USB client task calls USB Host client APIs, matching ESP-IDF's
  threading requirements.
- Print data is copied into a bounded queue and divided into 4 KiB USB transfers.
- Pending chunks are discarded after disconnect rather than unexpectedly
  printing later.
- Raw TCP accepts one client connection at a time.
- There is no authentication or encryption. Keep this device on a trusted LAN.
- The server does not implement SNMP, IPP, WSD, LPR, AirPrint, printer status
  queries, job persistence, or automatic retries.
- Wi-Fi credentials are currently compile-time configuration values; changing
  networks requires rebuilding/flashing.

## Why not use the normal Zebra network installer?

The ESP32 is not emulating a complete Zebra network printer. It exposes only a
raw JetDirect-style byte stream and a small HTTP API. Windows therefore needs a
manually created Standard TCP/IP Port, while the UPS driver supplies the label
rendering and ZPL/EPL output.
