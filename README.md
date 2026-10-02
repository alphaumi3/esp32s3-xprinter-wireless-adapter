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

# Cấu hình dự án
idf.py menuconfig
