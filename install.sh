#!/bin/bash
set -e

REPO_URL="https://github.com/DatDangg/uniiki.git"
INSTALL_DIR="$HOME/.uniiki"
BIN_DIR="$HOME/.local/bin"

echo "=== 🇻🇳 Đang cài đặt Uniiki (Bộ Gõ Tiếng Việt Ubuntu/Linux) ==="

# 1. Kiểm tra Git & Python3
if ! command -v git &> /dev/null; then
    echo "❌ Lỗi: Cần cài đặt git trước (chạy: sudo apt install git)"
    exit 1
fi

if ! command -v python3 &> /dev/null; then
    echo "❌ Lỗi: Không tìm thấy Python3"
    exit 1
fi

# 2. Tải hoặc Cập nhật mã nguồn vào ~/.uniiki
if [ -d "$INSTALL_DIR/.git" ]; then
    echo "[1/4] Đang cập nhật Uniiki từ git tại $INSTALL_DIR..."
    git -C "$INSTALL_DIR" pull origin main || true
elif [ -d "$INSTALL_DIR" ]; then
    echo "[1/4] Đang đồng bộ Uniiki tại $INSTALL_DIR..."
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    if [ "$SCRIPT_DIR" != "$INSTALL_DIR" ]; then
        cp -r "$SCRIPT_DIR"/* "$INSTALL_DIR/" 2>/dev/null || true
    fi
else
    echo "[1/4] Đang tải mã nguồn Uniiki vào $INSTALL_DIR..."
    git clone "$REPO_URL" "$INSTALL_DIR"
fi

# 3. Cài đặt Python Dependencies
echo "[2/4] Đang cài đặt thư viện Python..."
python3 -m pip install -r "$INSTALL_DIR/requirements.txt" --break-system-packages 2>/dev/null || python3 -m pip install -r "$INSTALL_DIR/requirements.txt"

# 4. Tạo lệnh 'uniiki' toàn hệ thống (Executable script)
mkdir -p "$BIN_DIR"
cat << 'EOF' > "$BIN_DIR/uniiki"
#!/bin/bash
python3 "$HOME/.uniiki/main.py" "$@"
EOF
chmod +x "$BIN_DIR/uniiki"
echo "[3/4] Đã tạo lệnh 'uniiki' trong $BIN_DIR"

# 5. Cấp quyền uinput cho evdev backend
if [ -e /dev/uinput ]; then
    sudo usermod -aG input $USER 2>/dev/null || true
fi

# 6. Thiết lập Autostart khi đăng nhập Ubuntu
mkdir -p ~/.config/autostart
cat << EOF > ~/.config/autostart/uniiki.desktop
[Desktop Entry]
Type=Application
Name=Uniiki
Exec=python3 $HOME/.uniiki/main.py
Icon=input-keyboard
Comment=Bộ gõ tiếng Việt không preedit cho Linux
Categories=Utility;
X-GNOME-Autostart-enabled=true
EOF
echo "[4/5] Đã thêm Uniiki vào Autostart."

# 7. Cấu hình biến môi trường bộ gõ (GTK/QT/XMODIFIERS) cho Terminal & Wayland
mkdir -p ~/.config/environment.d
cat << 'EOF' > ~/.config/environment.d/50-fcitx5.conf
GTK_IM_MODULE=fcitx
QT_IM_MODULE=fcitx
XMODIFIERS=@im=fcitx
EOF

for RC_FILE in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
    if [ -f "$RC_FILE" ] && ! grep -q "GTK_IM_MODULE=fcitx" "$RC_FILE"; then
        cat << 'EOF' >> "$RC_FILE"

# Fcitx5 / Uniiki environment variables
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
EOF
    fi
done
echo "[5/5] Đã thiết lập biến môi trường bộ gõ cho Terminal & Wayland."

echo ""
echo "=== ✨ Cài đặt Uniiki hoàn tất! ==="
echo "📌 Bạn có thể bật bộ gõ bằng lệnh: uniiki"
echo "📌 Uniiki sẽ tự động chạy cùng hệ thống khi khởi động."

