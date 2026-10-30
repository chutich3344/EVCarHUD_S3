# EVCarHUD cho board ESP32-2432S028 (2.8" 320x240)

Firmware HUD cho board "Cheap Yellow Display" ESP32-2432S028 (ESP32-WROOM, màn 2.8" ILI9341/ST7789, cảm ứng XPT2046).
Điện thoại chạy **Waze Mod** gửi dữ liệu dẫn đường (giao thức HLP/1) qua **Bluetooth LE**, HUD giải mã và hiện giao diện dạng thẻ:

![Giao diện](docs/giao-dien.png)

- Thẻ dẫn đường: mũi tên rẽ, khoảng cách, kiểu rẽ, tên đường, thẻ "Tiếp theo".
- Tốc độ hiện tại: xanh lá, vàng khi cách giới hạn ≤ 5 km/h, đỏ khi vượt (kèm viền đỏ nhấp nháy, LED đỏ, loa kêu 2 tiếng).
- Biển giới hạn tốc độ và biển báo gần nhất (khu dân cư, cấm vượt, cấm rẽ...).
- Cảnh báo gần nhất (cảnh sát, camera, tai nạn, kẹt xe...) và giờ đến, quãng đường còn lại.
- Ban ngày / Ban đêm: chạm màn hình hoặc bấm BOOT để đổi Tự động → Ngày → Đêm (tự động theo giờ điện thoại, 6:00–18:00 là ngày).

## 1. Cài ESP-IDF (một lần)

Cài **ESP-IDF 5.3 trở lên** bằng bộ cài Windows: https://dl.espressif.com/dl/esp-idf/ (chọn bản 5.3.x hoặc 5.4.x).
Sau khi cài, mở **ESP-IDF 5.x CMD** (hoặc PowerShell) từ menu Start.

## 2. Build

```bat
cd H:\Projects\ECarHUD_S3
idf.py set-target esp32
idf.py build
```

Lần đầu `idf.py` tự tải LVGL và driver màn hình/cảm ứng (cần mạng).

## 3. Nạp

**Cách 1 – cắm USB nạp thẳng** (thay `COM5` bằng cổng của board):

```bat
idf.py -p COM5 flash monitor
```

**Cách 2 – nạp qua trang web evcarhud.com**: gộp thành 1 file rồi tải lên trang `/admin` với địa chỉ nạp **0x0**:

```bat
idf.py merge-bin -o evcarhud-full.bin
```

File nằm ở `build\evcarhud-full.bin`.

## 4. Lần chạy đầu: màn kiểm tra màu

Khi khởi động, màn hình hiện 4 ô: **ĐỎ** (trên trái), **XANH LÁ** (trên phải), **TRẮNG** (dưới trái), **XANH DƯƠNG** (dưới phải), và LED nháy đỏ → xanh lá → xanh dương.
Nếu sai, mở `idf.py menuconfig` → **EVCarHUD → Man hinh**:

| Hiện tượng | Sửa |
|---|---|
| Màn trắng / không hiện gì | Đổi chip **ILI9341 ↔ ST7789** (board 2 cổng USB hay dùng ST7789) |
| Đỏ và xanh dương đổi chỗ | Bật/tắt **Thu tu mau BGR** |
| Màu như ảnh âm bản | Bật/tắt **Dao mau** |
| Hình bị lật ngang / dọc | Bật/tắt **Lat ngang** / **Lat doc** |
| Không cần màn kiểm tra nữa | Tắt **Man kiem tra mau** |

Sửa xong chạy lại `idf.py build flash`.

## 5. Kết nối Waze Mod

Trên điện thoại: **Waze Mod → HUD → Bluetooth LE**, chọn thiết bị **EVCarHUD**. Chưa kết nối thì màn hình hiện "MẤT TÍN HIỆU" kèm hướng dẫn.

## 6. Chế độ trình diễn (không cần điện thoại)

Giữ nút **BOOT 3 giây**: bật/tắt chế độ trình diễn (dữ liệu giả, chạy vòng 42 giây), board tự khởi động lại. Dùng để trưng bày hoặc kiểm tra màn hình.

## Chân GPIO (mặc định)

| Phần | Chân |
|---|---|
| LCD (SPI2) | SCLK 14, MOSI 13, MISO 12, CS 15, DC 2, đèn nền 21 |
| Cảm ứng XPT2046 (SPI3) | SCLK 25, MOSI 32, MISO 39, CS 33 |
| LED RGB | 4 / 16 / 17 (mức thấp là sáng) |
| Loa | 26 |
| Quang trở (tùy chọn) | 34 |

Đổi chân trong `idf.py menuconfig` → **EVCarHUD → Chan GPIO**.

## Cho người phát triển

- `main/dashboard.c`: giao diện (LVGL 9), chỉ nhận dữ liệu qua các hàm `ui_set_*`.
- `main/dashboard_hlp.c`, `main/hlp.c`: giải mã HLP/1 của Waze Mod. `main/ble_link.c`: Bluetooth LE (NimBLE).
- `main/board.c`: màn hình, cảm ứng, đèn nền, LED, loa.
- `tools/pc_preview/build.sh`: chụp ảnh giao diện trên PC (Linux/WSL), không cần board.
- `tools/fonts/gen_fonts.sh`: tạo lại font (Inter, JetBrains Mono) – cần Node.js.
