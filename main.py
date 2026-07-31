#!/usr/bin/env python3
"""
Uniiki - Trình bộ gõ Tiếng Việt toàn hệ thống trên Ubuntu (Không Preedit)
Main Entry Point
"""

import sys
import os
import argparse

sys.path.insert(0, os.path.abspath(os.path.dirname(__file__)))

def main():
    parser = argparse.ArgumentParser(description="Uniiki System-wide Vietnamese IME without Preedit")
    parser.add_argument('--cli', action='store_true', help="Run in CLI headless mode (without System Tray icon)")
    parser.add_argument('--mode', choices=['telex', 'vni'], default='telex', help="Default Vietnamese typing mode")
    parser.add_argument('--backend', choices=['auto', 'evdev', 'pynput'], default='auto', help="Keyboard backend to use")
    args = parser.parse_args()

    print("=" * 60)
    print("      UNIIKI - BỘ GÕ TIẾNG VIỆT TOÀN HỆ THỐNG (KHÔNG PREEDIT)")
    print("============================================================")
    print(" • Không có Preedit underline (gạch chân / ô vuông xem trước)")
    print(" • Tương thích mọi app trên Ubuntu (Chrome, VS Code, Terminal...)")
    print(" • Phím tắt chuyển đổi VN/EN: Ctrl + Shift")
    print("=" * 60)

    if args.cli:
        from src.daemon import UniikiDaemon

        daemon = UniikiDaemon(mode=args.mode, enabled=True, backend=args.backend)
        if daemon.start():
            try:
                import time
                while True:
                    time.sleep(1)
            except KeyboardInterrupt:
                daemon.stop()
    else:
        from src.app import UniikiApp

        app = UniikiApp(backend=args.backend)
        app.run()

if __name__ == '__main__':
    main()
