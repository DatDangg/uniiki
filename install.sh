#!/bin/bash
set -e

echo "=== 🇻🇳 Cài đặt Uniiki ==="

# 1. Cài đặt Python dependencies
if command -v python3 &> /dev/null; then
    echo "[1/3] Đang cài đặt thư viện Python..."
    python3 -m pip install -r requirements.txt --break-system-packages 2>/dev/null || python3 -m pip install -r requirements.txt
else
    echo "Lỗi: Không tìm thấy Python3!"
    exit 1
fi

# 2. Thiết lập quyền uinput (cho evdev backend)
echo "[2/3] Kiểm tra quyền uinput..."
if [ -e /dev/uinput ]; then
    sudo usermod -aG input $USER 2>/dev/null || true
    echo "Lưu ý: Nếu dùng backend evdev, bạn có thể cần đăng xuất và đăng nhập lại để quyền uinput có hiệu lực."
fi

# 3. Setup Autostart (tùy chọn)
read -p "Bạn có muốn tự động chạy Uniiki khi khởi động máy không? (y/N): " choice
if [[ "$choice" =~ ^[Yy]$ ]]; then
    mkdir -p ~/.config/autostart
    cp uniiki.desktop ~/.config/autostart/
    echo "✅ Đã thêm Uniiki vào Autostart."
fi

echo "=== ✨ Cài đặt hoàn tất! ==="
echo "Bạn có thể khởi chạy bằng lệnh: python3 main.py"
