"""Uniiki system tray and Vietnamese input settings for Ubuntu."""

import sys
import os
import threading
from PIL import Image, ImageDraw, ImageFont

if 'PYSTRAY_BACKEND' not in os.environ:
    try:
        import gi
        gi.require_version('AppIndicator3', '0.1')
    except Exception:
        try:
            import gi
            gi.require_version('AyatanaAppIndicator3', '0.1')
        except Exception:
            os.environ['PYSTRAY_BACKEND'] = 'gtk'

try:
    import gi
    gi.require_version('Gtk', '3.0')
    from gi.repository import GLib
    HAS_GLIB = True
except Exception:
    HAS_GLIB = False

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
        self.lang = 'VI'
        self.modern_tone = True
        self.backend = backend
        
        self.daemon = UniikiDaemon(
            mode=self.mode,
            backend=self.backend
        )
        self.daemon.on_language_changed = self._on_daemon_lang_changed
        self.icon = None

    def _create_tray_icon(self, lang='VI'):
        """Generate tray icon showing VI or EN mode."""
        width = 64
        height = 64
        image = Image.new('RGBA', (width, height), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)

        if lang == 'VI':
            bg_color = (79, 70, 229, 255)   # Indigo for Vietnamese mode
            text = "VI"
        else:
            bg_color = (107, 114, 128, 255) # Slate gray for English mode
            text = "EN"

        draw.rounded_rectangle([4, 4, width - 4, height - 4], radius=16, fill=bg_color)

        text_color = (255, 255, 255, 255)
        try:
            font = ImageFont.truetype("DejaVuSans-Bold.ttf", 30)
        except IOError:
            try:
                font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 30)
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

    def _on_daemon_lang_changed(self, new_lang):
        self.lang = new_lang
        if HAS_GLIB:
            GLib.idle_add(self._update_tray_ui)
        else:
            self._update_tray_ui()

    def _update_tray_ui(self):
        if self.icon:
            self.icon.icon = self._create_tray_icon(self.lang)
            self.icon.title = f"Uniiki - {self.lang}"
            self.icon.menu = self._build_menu()

    def set_lang(self, lang):
        self.lang = lang
        self.daemon.set_language(lang)
        self._update_tray_ui()

    def toggle_lang(self):
        new_lang = self.daemon.toggle_language()
        self.lang = new_lang
        self._update_tray_ui()

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

    def _build_menu(self):
        return Menu(
            item('🚀 Uniiki - Bộ gõ tiếng Việt', None, enabled=False),
            Menu.SEPARATOR,
            item('🇻🇳 Tiếng Việt (VI)', lambda icon, item: self.set_lang('VI'), checked=lambda item: self.lang == 'VI'),
            item('🇬🇧 Tiếng Anh (EN)', lambda icon, item: self.set_lang('EN'), checked=lambda item: self.lang == 'EN'),
            Menu.SEPARATOR,
            item('Kiểu gõ: Telex', self.set_telex, checked=lambda item: self.mode == 'telex'),
            item('Kiểu gõ: VNI', self.set_vni, checked=lambda item: self.mode == 'vni'),
            Menu.SEPARATOR,
            item('Đặt dấu kiểu mới (hòa, thủy)', self.toggle_modern_tone, checked=lambda item: self.modern_tone),
            Menu.SEPARATOR,
            item('⌨️ Phím tắt đổi VI/EN: Ctrl + Shift', None, enabled=False),
            Menu.SEPARATOR,
            item('❌ Thoát Uniiki', self.quit_app)
        )

    def run(self):
        # Start input daemon
        self.daemon.start()

        if not HAS_PYSTRAY:
            print("[Uniiki App] 'pystray' package not installed. Running daemon in headless CLI mode.")
            print("Press Ctrl+Shift to toggle between VI and EN modes.")
            print("Press Ctrl+C to stop.")
            try:
                while True:
                    threading.Event().wait(1)
            except KeyboardInterrupt:
                self.daemon.stop()
            return

        icon_image = self._create_tray_icon(self.lang)
        self.icon = pystray.Icon(
            "Uniiki",
            icon_image,
            f"Uniiki - {self.lang}",
            self._build_menu(),
            default_action=lambda icon, item: self.toggle_lang()
        )
        print("[Uniiki App] System tray application started on Ubuntu top bar!")
        self.icon.run()

if __name__ == '__main__':
    app = UniikiApp()
    app.run()
