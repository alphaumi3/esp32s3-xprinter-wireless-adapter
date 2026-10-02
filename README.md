# ESP32-S3 Wireless Print Server for XPrinter XP-420B (USB Host + BLE + Wi-Fi)

Chuyển đổi máy in mã vạch nhiệt **XPrinter XP-420B** (và các dòng máy in nhãn USB tương tự) thành máy in không dây đa năng hỗ trợ cả **Bluetooth Low Energy (BLE)** và **Wi-Fi (Raw TCP 9100 / HTTP POST)** chỉ với một vi điều khiển **ESP32-S3**.

Dự án phát triển dựa trên nền tảng USB Host Printer Driver từ [esp32-zp450-print-server](https://github.com/mckinlk/esp32-zp450-print-server), bổ sung stack **NimBLE (Nordic UART Service)** và tinh chỉnh để tương thích tối đa với máy in XPrinter XP-420B.

---

## 🌟 Tính năng chính

- **USB Host Native**: Giao tiếp trực tiếp với cổng USB máy in qua chuẩn USB Printer Class (Bulk OUT Endpoint), không cần can thiệp mạch phần cứng TX/RX bên trong máy in.
- **Bluetooth BLE UART**: Sử dụng Nordic UART Service (NUS), tương thích với hầu hết các app in mã vạch/vận đơn trên Android & iOS (RawBT, PrintHand, Serial Bluetooth Terminal,...).
- **Wi-Fi Print Server**:
  - Hỗ trợ in Raw Socket qua cổng **Port 9100** (thêm máy in trực tiếp vào Windows/macOS không cần cắm dây).
  - Hỗ trợ Web Server endpoint **HTTP POST `/print`** và kiểm tra trạng thái qua **`/health`**.
  - Tích hợp **mDNS** (`xp420b.local`).
- **Đèn LED RGB trạng thái (WS2812)**:
  - 🟣 **Tím**: Đang kết nối Wi-Fi.
  - 🔵 **Xanh dương**: Đã có Wi-Fi, chưa cắm máy in.
  - 🟢 **Xanh lá**: Đã kết nối máy in và sẵn sàng (USB Enumerated).
  - 💠 **Xanh lơ (Cyan)**: Đang truyền dữ liệu in xuống máy in.
  - 🔴 **Đỏ**: Lỗi truyền nhận hoặc đầy hàng đợi.

---

## 🛠️ Yêu cầu phần cứng

1. **Kit ESP32-S3 DevKit** (có 2 cổng Type-C: UART/COM và USB-OTG). Flash tối thiểu 4MB.
2. **Cáp USB-OTG Type-C sang USB-A (hoặc Type-B)** để cắm từ ESP32-S3 sang cổng USB của máy in.
3. **Nguồn 5V ổn định**: Cấp nguồn đủ dòng (khuyên dùng $\ge 1.5A$) vào chân 5V/VBUS của kit ESP32-S3.
4. **Máy in mã vạch XPrinter XP-420B** (hoặc các dòng Xprinter / Gprinter tương đương dùng lệnh TSPL/ESC-POS).

---

## 🚀 Hướng dẫn cài đặt & Nạp Firmware

### 1. Chuẩn bị môi trường
- Tải và cài đặt **ESP-IDF v5.5 (Offline Installer)** cho Windows từ trang chủ Espressif.
- Sử dụng cửa sổ dòng lệnh **ESP-IDF 5.5 CMD**.

### 2. Tải mã nguồn & cấu hình
```bash
git clone [https://github.com/](https://github.com/)<your-username>/esp32s3-xprinter-wireless-adapter.git
cd esp32s3-xprinter-wireless-adapter

# Thiết lập target là ESP32-S3
idf.py set-target esp32s3
Trong giao diện `menuconfig`:

1. **Cấu hình Wi-Fi & Máy in**:
   - Vào mục cấu hình máy in/server (ví dụ: `ZP450 print server`): điền **Wi-Fi SSID**, **Mật khẩu**, **Hostname** (ví dụ `xp420b`), chân **GPIO cho đèn LED RGB** (ví dụ `GPIO 48`).
2. **Cấu hình Bluetooth (NimBLE)**:
   - Vào `Component config` → `Bluetooth`: nhấn phím cách `[*]` để kích hoạt **Bluetooth**.
   - Vào `Bluetooth Host`: chọn **NimBLE - BLE only**.
3. **Mở rộng phân vùng (Bắt buộc)**:
   - Vào `Partition Table` → chọn **Single factory app (large), no OTA** (để nới rộng phân vùng lên 2MB, tránh lỗi tràn bộ nhớ `overflow`).
4. Nhấn **S** để lưu và **Q** để thoát.

### 3. Biên dịch và nạp code

Cắm cổng **COM/UART** của ESP32-S3 vào máy tính và chạy:

```bash
idf.py -p COM5 flash monitor
```

*(Thay `COM5` bằng cổng COM thực tế của bạn).*

---

## 🖨️ Hướng dẫn sử dụng

### 1. In thử qua mạng Wi-Fi (HTTP / PowerShell)

* Mở trình duyệt kiểm tra: `http://xp420b.local/health` → phải hiện `{"printer":"online", ...}`.
* Gửi lệnh in thử TSPL qua PowerShell:

```powershell
$cmd = "SIZE 40 mm, 30 mm`r`nGAP 2 mm, 0 mm`r`nCLS`r`nTEXT 50,50,`"3`",0,1,1,`"TEST XPRINTER`"`r`nPRINT 1,1`r`n"
[System.IO.File]::WriteAllBytes("$HOME\test.bin", [System.Text.Encoding]::ASCII.GetBytes($cmd))
curl.exe --data-binary "@$HOME\test.bin" http://xp420b.local/print
```

### 2. In qua Bluetooth Low Energy (BLE)

* **Tên thiết bị BLE mặc định**: `XP-420B_BLE`
* Sử dụng app **Serial Bluetooth Terminal** (tab BLE) hoặc **RawBT**:
  * Quét và kết nối tới `XP-420B_BLE`.
  * Service: **Nordic UART Service** (`6E400001-B5A3-F393-E0A9-E50E24DCCA9E`).
  * Gửi mã lệnh in TSPL:

```text
SIZE 40 mm, 30 mm
GAP 2 mm, 0 mm
CLS
TEXT 50,50,"3",0,1,1,"BLE PRINT OK"
PRINT 1,1
```

### 3. Thêm máy in trực tiếp vào Windows (Raw Port 9100)

1. Mở **Printers & scanners** → **Add device** → **The printer that I want isn't listed**.
2. Chọn **Add a printer using an IP address or hostname**.
3. **Device type**: `TCP/IP Device` | **Hostname**: IP của ESP32 (hoặc `xp420b.local`).
4. Bỏ tick mục tự động dò driver → chọn driver **Xprinter XP-420B** đã cài trên máy tính.

---

## 🔧 Xử lý sự cố (Troubleshooting)

| Hiện tượng | Nguyên nhân | Cách khắc phục |
| --- | --- | --- |
| **Đèn LED tím đứng yên** | Không kết nối được Wi-Fi | Kiểm tra lại SSID và Password trong `menuconfig`. Đảm bảo Wi-Fi là băng tần 2.4GHz. |
| **Đèn LED xanh dương** | ESP32 chưa nhận máy in | Kiểm tra cáp USB-OTG, đảm bảo máy in đã bật nguồn và pad cấp nguồn VBUS đã được nối/hàn. |
| **Lỗi build: `app partition is too small`** | Kích thước nhị phân vượt quá 1MB mặc định | Vào `menuconfig` → `Partition Table` → chọn `Single factory app (large), no OTA`. |
| **Gửi lệnh in nhưng không in, không chớp Cyan** | Lệnh gửi thiếu ký tự ngắt dòng hoặc sai cổng | Dùng IP trực tiếp thay vì hostname `.local`; đảm bảo chuỗi lệnh in có đầy đủ `\r\n` và lệnh thực thi `PRINT 1,1`. |

---

## 📜 Giấy phép & Lời cảm ơn (Credits)

* Dựa trên mã nguồn gốc [esp32-zp450-print-server](https://github.com/mckinlk/esp32-zp450-print-server) của tác giả **mckinlk** (MIT License).
* Tích hợp thêm BLE UART stack bởi **Redline Co.,Ltd**.

```bash
idf.py -p COM5 flash monitor

# Cấu hình dự án
idf.py menuconfig
