# 🇻🇳 Uniiki - Bộ Gõ Tiếng Việt Toàn Hệ Thống Không Preedit (Ubuntu/Linux)

**Uniiki** là bộ gõ tiếng Việt chạy dạng daemon hệ thống trên Ubuntu/Linux, cho phép bạn gõ Telex & VNI trên **TẤT CẢ các ứng dụng** (Chrome, VS Code, Terminal, Telegram, Spotify...) mà **HOÀN TOÀN KHÔNG CẦN PREEDIT** (không có gạch chân, không có ô xem trước nhấp nháy).

---

## 🌟 Điểm nổi bật
- **Zero Preedit**: Ký tự tiếng Việt được biến đổi trực tiếp vào văn bản ngay tức thì.
- **Tương thích 100%**: Gõ mượt mà trên các app Electron (VS Code, Slack), trình duyệt, Terminal hay ứng dụng GTK/Qt.
- **System Tray Icon trên Ubuntu**: Chuyển đổi chế độ VN/EN, đổi kiểu gõ (Telex/VNI), chỉnh quy tắc đặt dấu (`hòa` vs `hoà`) ngay trên thanh công cụ Top Bar của Ubuntu.
- **Phím tắt nhanh**: Nhấn `Ctrl + Shift` để chuyển nhanh giữa chế độ Tiếng Việt (VN) và Tiếng Anh (EN).

---

## 🚀 Hướng dẫn cài đặt & Khởi chạy

> Trạng thái hiện tại: backend Python daemon chỉ phù hợp để thử nghiệm engine.
> Hướng phát triển chính đang chuyển sang Fcitx5 addon trong `fcitx5/`.
> Xem [docs/fcitx5-migration.md](docs/fcitx5-migration.md).

### 1. Cài đặt các thư viện cần thiết
Mở Terminal tại thư mục `uniiki` và chạy lệnh:
```bash
pip install -r requirements.txt
```

### 2. Khởi chạy Uniiki
* **Chạy giao diện System Tray (Khuyên dùng)**:
  ```bash
  python3 main.py
  ```
  Biểu tượng **[VN]** sẽ xuất hiện trên thanh Top Bar của Ubuntu.

* **Chạy chế độ Daemon không giao diện (CLI Mode)**:
  ```bash
  python3 main.py --cli --mode telex
  ```

* **Ép dùng backend Evdev/UInput (khuyên dùng để tránh lỗi nhân đôi chữ)**:
  ```bash
  python3 main.py --backend evdev
  ```
  Nếu lệnh báo `/dev/uinput is not writable`, hãy cấp quyền uinput cho user hiện tại rồi đăng xuất/đăng nhập lại.

* **Build thử addon Fcitx5 native (hướng phát triển chính)**:
  ```bash
  cmake -S fcitx5 -B build/fcitx5 -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build build/fcitx5
  ```

---

## ⌨️ Các kiểu gõ hỗ trợ

### Chế độ Telex (Mặc định)
- Dấu: `s` (sắc), `f` (huyền), `r` (hỏi), `x` (ngã), `j` (nặng), `z` (xóa dấu).
- Mũ/Móc: `aa` $\rightarrow$ `â`, `ee` $\rightarrow$ `ê`, `oo` $\rightarrow$ `ô`, `aw` $\rightarrow$ `ă`, `ow` $\rightarrow$ `ơ`, `uw` $\rightarrow$ `ư`, `dd` $\rightarrow$ `đ`, `w` $\rightarrow$ `ư`.

### Chế độ VNI
- Dấu: `1` (sắc), `2` (huyền), `3` (hỏi), `4` (ngã), `5` (nặng), `0` (xóa dấu).
- Mũ/Móc: `6` (mũ), `7` (móc), `8` (trăng), `9` (gạch `đ`).

---

## 🔄 Tự động chạy khi khởi động Ubuntu (Autostart)
Để Uniiki tự chạy mỗi khi bạn bật máy Ubuntu, chỉ cần tạo file autostart:
```bash
cp uniiki.desktop ~/.config/autostart/
```
