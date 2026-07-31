"""
Uniiki System Tray & Settings Application for Ubuntu
Renders a system tray indicator on Ubuntu top bar to toggle VN/EN modes and configure engine options.
"""

import sys
import os
import threading
from PIL import Image, ImageDraw, ImageFont

try:
    import pystray
    from pystray import MenuItem as item, Menu
    HAS_PYSTRAY = True
except Exception:
    HAS_PYSTRAY = False

from src.daemon import UniikiDaemon

class UniikiApp:
    def __init__(self, backend='auto'):
        self.mode = 'telex'
        self.enabled = True
        self.modern_tone = True
        self.backend = backend
        
        self.daemon = UniikiDaemon(
            mode=self.mode, 
            enabled=self.enabled,
            on_state_change=self._on_daemon_state_change,
            backend=self.backend
        )
        self.icon = None

    def _create_tray_icon(self, text="VN", is_active=True):
        """Generates a sleek, high-resolution tray icon image with VN or EN label."""
        width = 64
        height = 64
        image = Image.new('RGBA', (width, height), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)

        # Background badge: vibrant indigo for VN active, slate gray for EN inactive
        bg_color = (79, 70, 229, 255) if is_active else (100, 116, 139, 255) # #4F46E5 or #64748B
        draw.rounded_rectangle([4, 4, width - 4, height - 4], radius=16, fill=bg_color)

        # Text label (VN / EN)
        text_color = (255, 255, 255, 255)
        try:
            font = ImageFont.truetype("DejaVuSans-Bold.ttf", 30)
        except IOError:
            font = ImageFont.load_default()

        # Center text inside icon
        bbox = draw.textbbox((0, 0), text, font=font)
        tw = bbox[2] - bbox[0]
        th = bbox[3] - bbox[1]
        tx = (width - tw) / 2
        ty = (height - th) / 2 - 2
        draw.text((tx, ty), text, fill=text_color, font=font)

        return image

    def _on_daemon_state_change(self, is_enabled):
        self.enabled = is_enabled
        if self.icon:
            text = "VN" if is_enabled else "EN"
            self.icon.icon = self._create_tray_icon(text, is_enabled)
            self.icon.title = f"Uniiki Vietnamese IME: {'BẬT (VN)' if is_enabled else 'TẮT (EN)'}"

    def toggle_mode_click(self, icon, item):
        self.daemon.toggle_enable()

    def set_telex(self, icon, item):
        self.mode = 'telex'
        self.daemon.set_mode('telex')

    def set_vni(self, icon, item):
        self.mode = 'vni'
        self.daemon.set_mode('vni')

    def toggle_modern_tone(self, icon, item):
        self.modern_tone = not self.modern_tone
        self.daemon.engine.modern_tone = self.modern_tone

    def quit_app(self, icon, item):
        self.daemon.stop()
        if self.icon:
            self.icon.stop()
        sys.exit(0)

    def run(self):
        # Start input daemon
        self.daemon.start()

        if not HAS_PYSTRAY:
            print("[Uniiki App] 'pystray' package not installed. Running daemon in headless CLI mode.")
            print("Press Ctrl+C to stop.")
            try:
                while True:
                    threading.Event().wait(1)
            except KeyboardInterrupt:
                self.daemon.stop()
            return

        # Build System Tray Menu
        menu = Menu(
            item('🚀 Uniiki - Bộ gõ tiếng Việt (Không Preedit)', None, enabled=False),
            Menu.SEPARATOR,
            item('🇻🇳 Chế độ Tiếng Việt (VN)', self.toggle_mode_click, checked=lambda item: self.enabled),
            Menu.SEPARATOR,
            item('Kiểu gõ: Telex', self.set_telex, checked=lambda item: self.mode == 'telex'),
            item('Kiểu gõ: VNI', self.set_vni, checked=lambda item: self.mode == 'vni'),
            Menu.SEPARATOR,
            item('Đặt dấu kiểu mới (hòa, thủy)', self.toggle_modern_tone, checked=lambda item: self.modern_tone),
            Menu.SEPARATOR,
            item('❌ Thoát Uniiki', self.quit_app)
        )

        icon_image = self._create_tray_icon("VN", self.enabled)
        self.icon = pystray.Icon("Uniiki", icon_image, "Uniiki Vietnamese IME", menu)
        print("[Uniiki App] System tray application started on Ubuntu top bar!")
        self.icon.run()

if __name__ == '__main__':
    app = UniikiApp()
    app.run()
