#!/bin/bash
set -e

echo "=== 🗑️ Đang gỡ bỏ Uniiki khỏi hệ thống ==="

# 1. Dừng các tiến trình Uniiki Python daemon nếu đang chạy
pkill -f "main.py" 2>/dev/null || true
pkill -f "uniiki" 2>/dev/null || true

# 2. Xóa các file cài đặt Fcitx5 user-level
rm -f ~/.local/share/fcitx5/addon/uniiki.conf 2>/dev/null || true
rm -f ~/.local/share/fcitx5/inputmethod/uniiki.conf 2>/dev/null || true
rm -f ~/.local/share/icons/hicolor/scalable/apps/uniiki*.svg 2>/dev/null || true
rm -f ~/.local/lib/fcitx5/libuniiki.so 2>/dev/null || true

# 3. Xóa executable script, autostart, environment.d và thư mục mã nguồn user
rm -f ~/.local/bin/uniiki 2>/dev/null || true
rm -f ~/.config/autostart/uniiki.desktop 2>/dev/null || true
rm -f ~/.config/environment.d/50-fcitx5.conf 2>/dev/null || true
rm -rf ~/.uniiki 2>/dev/null || true

# 4. Xóa các file hệ thống Fcitx5 /usr (nếu có quyền sudo)
if [ "$EUID" -eq 0 ]; then
    rm -f /usr/lib/x86_64-linux-gnu/fcitx5/libuniiki.so 2>/dev/null || true
    rm -f /usr/lib/x86_64-linux-gnu/fcitx5/uniiki.so 2>/dev/null || true
    rm -f /usr/share/fcitx5/addon/uniiki.conf 2>/dev/null || true
    rm -f /usr/share/fcitx5/inputmethod/uniiki.conf 2>/dev/null || true
    rm -f /usr/share/icons/hicolor/scalable/apps/uniiki*.svg 2>/dev/null || true
else
    sudo rm -f /usr/lib/x86_64-linux-gnu/fcitx5/libuniiki.so \
            /usr/lib/x86_64-linux-gnu/fcitx5/uniiki.so \
            /usr/share/fcitx5/addon/uniiki.conf \
            /usr/share/fcitx5/inputmethod/uniiki.conf \
            /usr/share/icons/hicolor/scalable/apps/uniiki*.svg 2>/dev/null || true
fi

# 5. Tải lại cấu hình Fcitx5
fcitx5-remote -r 2>/dev/null || true

echo "=== ✨ Đã gỡ bỏ Uniiki hoàn toàn khỏi hệ thống! ==="
