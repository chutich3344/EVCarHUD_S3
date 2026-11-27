# Bàn giao: VietMap Live (VML) trên HUD 2.8" (CYD, ESP32 đời đầu)

Cập nhật 27/11/2026. Nhánh `claude/project-thread-3jpx6x` của repo EVCarHUD_S3 (PR #3). Đọc file này là đủ để làm tiếp.

## 1. Nguyên lý
HUD giả làm **VietMap H1V** qua Bluetooth thường (SPP, Bluedroid). App VietMap Live trên điện thoại tự nối tới thiết bị BT tên `VIETMAP_HUD_EVC` và gửi dữ liệu. ESP32-S3 / P4 chỉ có BLE nên không làm được trực tiếp (P4 dùng mạch cầu ESP32 DevKit, xem repo EVCarHUD_P4, luồng riêng).

- Build: `sdkconfig.vml` (Classic BT, **không BLE, không Wi-Fi, không PSRAM**). Code chính: `main/vml_link.c`. Giao diện dùng chung `main/dashboard*.c` (giao diện 2.9).
- CI: `.github/workflows/build-vml.yml`, chạy mỗi lần push nhánh → release `vml-28`, file `p2.8-vml-<ver>-<ngày>.bin` nạp ở **0x0**.
- Giả lập trên PC: `tools/pc_preview/preview_vml.c`.

## 2. Giao thức đã giải
Khung: `A5 5A 37 C3` | độ dài BE32 (tổng = len + 8) | ... | lệnh byte 12-13 | ... | 8 byte đuôi (chưa rõ có phải checksum).

| Lệnh | Ý nghĩa | Trạng thái |
|---|---|---|
| `0E00` (22 byte) | App hỏi thông tin → trả chuỗi 110 byte `MODEL:H1V,HW:1.6.4,FW:1.3.6,...` | OK |
| `0101` 29 byte | **byte 20 = biển tốc độ hiện tại** | OK, test thực tế 50/70 đúng |
| `0101` 25 byte | dữ liệu 3 byte `86 A0 xx`, xx = 0 khi đứng yên | **nghi là tốc độ**, chưa xác minh |
| `40FE` | chỉ dẫn: BE32 độ dài ảnh ở byte 14 (1 = không ảnh), JPEG 60x60, rồi mét BE32 + 1 byte độ dài nhãn + nhãn UTF-8 | OK |

`40FE`: nhãn `Tốc độ tiếp theo` = biển kế tiếp; ảnh có màu xanh dương = mũi tên rẽ (nhãn = tên đường); ảnh khác = cảnh báo (camera...).

## 3. Hành vi VML (quan trọng)
- **Chỉ gửi khi thay đổi**, hay ngắt/nối lại, và nối lại **không gửi lại** → HUD phải giữ dữ liệu (đang giữ 10 phút).
- Không có gói "đã qua biển/chỗ rẽ" → khoảng cách cũ bị treo (đã sửa ở v9: còn ≤30 m mà im 8 s/15 s thì ẩn).
- App mời "cập nhật firmware H1V" vì HUD báo FW 1.3.6 → vô hại, bấm Huỷ. Hướng sửa: báo FW cao hơn (cần biết 8 byte đuôi có phải checksum không).
- **Không gợi ý dùng fake GPS** (làm hỏng VML của chủ dự án).

## 4. Các bản
| Bản | Commit | Nội dung |
|---|---|---|
| v5 | b01fe67 | giữ dữ liệu qua ngắt/nối 10 phút |
| v6 | 955ddc7 | tên đường dòng trên khi dẫn đường |
| v7 | 3a2b1c7 | đọc gói 0101 25 byte, toast hex |
| v8 | 3941f28 | tự ghi log vào flash, lần bật sau hiện bảng LOG VML |
| v9 | xem git log | luôn hiện bảng LOG VML (cả khi trống), log ghi vào khe không chạy, ẩn biển tiếp/mũi tên đã qua |
| v10 | xem git log | không dẫn đường: thẻ cảnh báo chiếm cả cột phải (ảnh 72 px + khoảng cách + chữ, vd trạm thu phí + giá vé); ngắt VML > 1 phút xoá mũi tên rẽ; log thêm vùng chữ 64 KB (nhãn cảnh báo, tên đường, gói lạ hex) |

## 5. Log (không có Wi-Fi nên không gửi lên mạng)
Ghi vào 64 KB đầu khe firmware không chạy (v10: thêm 64 KB chữ ngay sau, mỗi dòng `giây loại nội-dung`) (thường `ota_1` = 0x200000). Bản ghi 8 byte `[loại][giây 24 bit][d0 d1 d2][0]`: 1 gói ngắn, 2 gói biển 29 B, 3 nối, 4 ngắt, 5 chỉ dẫn (d0 1 rẽ / 2 biển tiếp / 3 cảnh báo, d1d2 = m/10), 0x10 bật HUD. Nạp lại firmware ở 0x0 **không xoá** log.

**Lấy log bằng cáp USB:** `pip install esptool` rồi `python tools/vml_log/doc_log_vml.py [COMx]` → ra `log-vml-28-<ngày>.txt` (+ `.bin`). Không xoá log, rút cáp HUD chạy lại bình thường.

## 6. Việc còn mở (theo thứ tự)
1. **Tốc độ**: dòng HUD VietMap thật lấy tốc độ từ OBD → gần như chắc VML không gửi; hướng chính là GPS điện thoại qua BLE (bản 3.0). Vẫn dùng log (loại 1) đối chiếu tốc độ thực tế để xác minh byte cuối gói 25 byte (km/h? có hệ số?). Nếu đúng: `ui_set_speed()` từ gói này. Nếu VML không gửi tốc độ: phương án bản gộp 3.0 lấy GPS điện thoại qua BLE.
2. **Camera / cảnh báo**: app hiện camera 130 m, 156 m nhưng HUD không hiện. Log loại 5 d0=3 cho biết VML có gửi qua BT không. Nếu có mà không hiện → sửa `is_arrow()`/nhánh cảnh báo; nếu không gửi → giới hạn của VML.
3. **Ảnh làn đường** (khi không dẫn đường, mũi tên làn 314 m): kiểm tra tương tự mục 2.
4. Ẩn các toast chẩn đoán khi ổn định.
5. Báo FW cao hơn để hết mời cập nhật.
6. Bản gộp 3.0: SPP (VML) + BLE (Waze/GOFA/LANE) cùng lúc trên Bluedroid; test lại GOFA kỹ.
7. Logo VML chính thức.

## 7. Quy tắc làm việc của chủ dự án
Trả lời tiếng Việt. Mọi thay đổi giao diện kèm ảnh giả lập. Trước mỗi bản build: liệt kê lỗi + cách sửa để duyệt, rồi build gộp 1 lần (không build lắt nhắt). Tên file .bin theo mục tiêu + ngày.
