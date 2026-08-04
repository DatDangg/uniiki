# 🇻🇳 Uniiki - Bộ Gõ Tiếng Việt Toàn Hệ Thống Không Preedit (Linux/Ubuntu)

[![Version](https://img.shields.io/badge/version-v1.2.0-blue.svg)]()
[![Linux](https://img.shields.io/badge/OS-Linux%20%2F%20Ubuntu-orange?logo=ubuntu)](https://ubuntu.com)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=cplusplus)](https://isocpp.org/)
[![Fcitx5](https://img.shields.io/badge/Framework-Fcitx5-green)](https://fcitx-im.org/)
[![Python](https://img.shields.io/badge/Python-3.10%2B-blue?logo=python)](https://python.org)
[![Zero Preedit](https://img.shields.io/badge/Feature-Zero--Preedit-brightgreen)]()

**Uniiki** là bộ gõ tiếng Việt hiện đại chạy trên Ubuntu/Linux, hỗ trợ gõ **Telex** mượt mà trên **TẤT CẢ các ứng dụng** (Chrome, VS Code, Terminal, Telegram, Slack, Spotify...) mà **HOÀN TOÀN KHÔNG CẦN PREEDIT** (không có gạch chân xem trước, không bị nhấp nháy ô nhập liệu hay xung đột khung gõ).

---

## 🌟 Tại Sao Chọn Uniiki?

* ⚡ **Zero Preedit**: Ký tự tiếng Việt được biến đổi và đưa thẳng vào văn bản ngay tức thì mà không qua ô gạch chân xem trước.
* ⌨️ **Đổi Chế Độ Gõ Siêu Nhanh (Ctrl + Shift)**: Chuyển đổi qua lại giữa tiếng Việt (**VI**) và tiếng Anh (**EN**) dễ dàng bằng phím tắt `Ctrl + Shift`.
* 🎨 **System Tray Icon (VI / EN)**: Hiển thị trực quan trạng thái gõ hiện tại trên thanh tác vụ Ubuntu (Icon tím **VI** / Icon xám **EN**).
* 🚀 **Tích Hợp Fcitx5 Native Addon**: Tích hợp trực tiếp vào trình quản lý bộ gõ Fcitx5 cho độ trễ cực thấp và độ ổn định tuyệt đối.
* 🎯 **Tương Thích 100%**: Hoạt động hoàn hảo trên ứng dụng Electron (VS Code, Discord), ứng dụng GTK/Qt, Trình duyệt web và Terminal (X11 & Wayland).
* ⚙️ **Quy Tắc Đặt Dấu**: Hỗ trợ chuẩn dấu mới (`hòa`, `thủy`) cũng như chuẩn cũ (`hoà`, `thuỷ`).
* 🔄 **Autostart**: Dễ dàng thiết lập tự động khởi động cùng hệ thống.

---

## ⚡ Cài Đặt Nhanh

### 🚀 Cách 1: Cài Đặt Tự Động (Khuyên Dùng)

Chạy lệnh duy nhất sau trong Terminal để tự động tải, biên dịch và cấu hình biến môi trường bộ gõ cho **Terminal, VS Code & Wayland**:

```bash
curl -fsSL https://raw.githubusercontent.com/DatDangg/uniiki/main/install.sh | bash
```
*(Nếu đã clone repository về máy, bạn chỉ cần mở terminal tại thư mục dự án và chạy: `./install.sh`)*

---

### 🛠️ Cách 2: Biên Dịch & Cài Đặt Thủ Công (CMake)

Yêu cầu cài đặt các gói phụ thuộc trên Ubuntu/Debian:
```bash
sudo apt update
sudo apt install -y cmake extra-cmake-modules g++ pkg-config fcitx5 libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev
```

Biên dịch và cài đặt Addon Native vào hệ thống:
```bash
git clone https://github.com/DatDangg/uniiki.git
cd uniiki
cmake -S fcitx5 -B build/fcitx5 -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/fcitx5
sudo cmake --install build/fcitx5
sudo gtk-update-icon-cache -f -t /usr/share/icons/hicolor/ && fcitx5-remote -r
```

> **📌 BẮT BUỘC: Cấu hình biến môi trường để gõ được tiếng Việt trên Terminal & Wayland:**
> Để Terminal (GNOME Terminal, VS Code, Alacritty...) nhận bộ gõ Fcitx5 trên Wayland, chạy 2 lệnh sau:
> ```bash
> mkdir -p ~/.config/environment.d
> echo -e "GTK_IM_MODULE=fcitx\nQT_IM_MODULE=fcitx\nXMODIFIERS=@im=fcitx" > ~/.config/environment.d/50-fcitx5.conf
> echo -e "\nexport GTK_IM_MODULE=fcitx\nexport QT_IM_MODULE=fcitx\nexport XMODIFIERS=@im=fcitx" >> ~/.bashrc
> source ~/.bashrc
> ```

> **Sau khi cài đặt xong:**
> 1. Mở ứng dụng **Fcitx5 Configuration** (Cấu hình Fcitx5).
> 2. Nhấn nút **`+`** (Thêm bộ gõ), bỏ chọn *"Only Show Current Language"*.
> 3. Tìm từ khóa **Uniiki** và chọn **Add**.

---

## ⌨️ Bảng Quy Tắc Gõ Telex

| Thao tác | Phím gõ | Kết quả ví dụ |
| :--- | :--- | :--- |
| **Dấu thanh** | `s` (Sắc), `f` (Huyền), `r` (Hỏi), `x` (Ngã), `j` (Nặng), `z` (Xóa dấu) | `as` $\rightarrow$ `á`, `af` $\rightarrow$ `à`, `az` $\rightarrow$ `a` |
| **Mũ chữ** | `aa` $\rightarrow$ `â`, `ee` $\rightarrow$ `ê`, `oo` $\rightarrow$ `ô` | `daang` $\rightarrow$ `đang` |
| **Móc / Trăng**| `aw` $\rightarrow$ `ă`, `ow` $\rightarrow$ `ơ`, `uw` / `w` $\rightarrow$ `ư` | `nawmg` $\rightarrow$ `năm`, `muonw` $\rightarrow$ `mượn` |
| **Chữ Đ** | `dd` $\rightarrow$ `đ` | `ddang` $\rightarrow$ `đang` |

---

## 🔄 Tự Động Khởi Động (Autostart)

Để Uniiki tự chạy mỗi khi bạn đăng nhập Ubuntu:
```bash
mkdir -p ~/.config/autostart
cp uniiki.desktop ~/.config/autostart/
```

---

## 🗑️ Gỡ Bỏ (Uninstall)

Để gỡ bỏ hoàn toàn Uniiki khỏi hệ thống, bạn chạy duy nhất lệnh này trong Terminal:
```bash
curl -fsSL https://raw.githubusercontent.com/DatDangg/uniiki/main/uninstall.sh | bash
```

---

## 📄 Giấy Phép (License)

Dự án được phân phối dưới giấy phép open-source. Chi tiết tham khảo trong tệp repository.
