"""
Uniiki System Daemon
Captures system-wide keyboard events and injects direct unicode replacements without preedit.
Features Synchronous GTK Main-Loop Event Flushing for 100% reliable clipboard injection.
"""

import time
import threading
import sys
import os
import subprocess
import signal
import atexit

try:
    from pynput import keyboard
    from pynput.keyboard import Key, KeyCode, Controller
    HAS_PYNPUT = True
except ImportError:
    HAS_PYNPUT = False

try:
    import evdev
    from evdev import InputDevice, categorize, ecodes, UInput
    HAS_EVDEV = True
except ImportError:
    HAS_EVDEV = False

try:
    import gi
    gi.require_version('Gtk', '3.0')
    from gi.repository import Gtk, Gdk
    HAS_GI_GTK = True
except Exception:
    HAS_GI_GTK = False

try:
    import pyperclip
    HAS_PYPERCLIP = True
except ImportError:
    HAS_PYPERCLIP = False

from src.engine import VietnameseEngine

if HAS_EVDEV:
    CHAR_TO_EVDEV = {
        'a': ecodes.KEY_A, 'b': ecodes.KEY_B, 'c': ecodes.KEY_C, 'd': ecodes.KEY_D,
        'e': ecodes.KEY_E, 'f': ecodes.KEY_F, 'g': ecodes.KEY_G, 'h': ecodes.KEY_H,
        'i': ecodes.KEY_I, 'j': ecodes.KEY_J, 'k': ecodes.KEY_K, 'l': ecodes.KEY_L,
        'm': ecodes.KEY_M, 'n': ecodes.KEY_N, 'o': ecodes.KEY_O, 'p': ecodes.KEY_P,
        'q': ecodes.KEY_Q, 'r': ecodes.KEY_R, 's': ecodes.KEY_S, 't': ecodes.KEY_T,
        'u': ecodes.KEY_U, 'v': ecodes.KEY_V, 'w': ecodes.KEY_W, 'x': ecodes.KEY_X,
        'y': ecodes.KEY_Y, 'z': ecodes.KEY_Z,
        '0': ecodes.KEY_0, '1': ecodes.KEY_1, '2': ecodes.KEY_2, '3': ecodes.KEY_3,
        '4': ecodes.KEY_4, '5': ecodes.KEY_5, '6': ecodes.KEY_6, '7': ecodes.KEY_7,
        '8': ecodes.KEY_8, '9': ecodes.KEY_9,
    }
else:
    CHAR_TO_EVDEV = {}

class UniikiDaemon:
    def __init__(self, mode='telex', backend='auto'):
        self.engine = VietnameseEngine(mode=mode, modern_tone=True)
        self.backend = backend
        self.lang = 'VI'
        self.on_language_changed = None

        self.ctrl_pressed = False
        self.shift_pressed = False
        self.ctrl_shift_combo_active = False
        self.ctrl_shift_interrupted = False
        
        self.keyboard_controller = Controller() if HAS_PYNPUT else None
        self.listener = None
        self.is_running = False
        self.pressed_keys = set()
        self.consumed_codes = set()
        self.is_injecting = False
        self.pynput_pre_inject_delay = float(os.environ.get('UNIIKI_PYNPUT_PRE_DELAY', '0.035'))
        self.pynput_key_delay = float(os.environ.get('UNIIKI_PYNPUT_KEY_DELAY', '0.012'))
        self.pynput_inject_lock = threading.Lock()

        self.grabbed_devices = []
        self.uinput_device = None

        if HAS_GI_GTK:
            try:
                self.gtk_clipboard = Gtk.Clipboard.get(Gdk.SELECTION_CLIPBOARD)
            except Exception:
                self.gtk_clipboard = None
        else:
            self.gtk_clipboard = None

        atexit.register(self.stop)
        try:
            signal.signal(signal.SIGINT, self._handle_signal)
            signal.signal(signal.SIGTERM, self._handle_signal)
        except Exception:
            pass

    def _handle_signal(self, signum, frame):
        print(f"[Uniiki Daemon] Received signal {signum}, performing emergency ungrab...")
        self.stop()
        sys.exit(0)

    def set_mode(self, mode):
        self.engine.set_mode(mode)
        print(f"[Uniiki Daemon] Input mode changed to: {mode}")

    def set_language(self, lang):
        if lang in ['VI', 'EN'] and self.lang != lang:
            self.lang = lang
            self.engine.reset_buffer()
            print(f"[Uniiki Daemon] Input language switched to: {self.lang}")
            if callable(self.on_language_changed):
                try:
                    self.on_language_changed(self.lang)
                except Exception as e:
                    print(f"[Uniiki Daemon Warning] Language callback error: {e}")

    def toggle_language(self):
        new_lang = 'EN' if self.lang == 'VI' else 'VI'
        self.set_language(new_lang)
        return self.lang

    def start(self):
        self.is_running = True

        if self.backend in ['auto', 'evdev'] and HAS_EVDEV and os.access('/dev/uinput', os.W_OK):
            print("[Uniiki Daemon] Using Safe Evdev Keyboard Backend...")
            if self._start_evdev():
                return True

        if self.backend == 'evdev':
            print("[Uniiki Daemon] Error: evdev backend requested but /dev/uinput is not writable.")
            print("[Uniiki Daemon] Add your user to the input group and enable uinput access, then log out/in.")
            return False

        if self.backend in ['auto', 'pynput'] and HAS_PYNPUT:
            print("[Uniiki Daemon] Using Pynput Backend...")
            print("[Uniiki Daemon Warning] Pynput is a fallback backend and may duplicate letters in fast typing.")
            self.listener = keyboard.Listener(
                on_press=self._on_press,
                on_release=self._on_release
            )
            self.listener.start()
            print("[Uniiki Daemon] Daemon listening system-wide!")
            return True

        print("[Uniiki Daemon] Error: Neither pynput nor evdev could be started.")
        return False

    def stop(self):
        if not self.is_running and not self.grabbed_devices:
            return
        self.is_running = False
        
        for dev in self.grabbed_devices:
            try:
                dev.ungrab()
                print(f"[Uniiki Daemon] Ungrabbed keyboard device {dev.name} successfully.")
            except Exception:
                pass
        self.grabbed_devices.clear()

        if self.uinput_device:
            try:
                self.uinput_device.close()
            except Exception:
                pass
            self.uinput_device = None
        print("[Uniiki Daemon] Daemon safely stopped and keyboard released.")

    # ==================== PYNPUT BACKEND ====================
    def _on_press(self, key):
        if not self.is_running or self.is_injecting:
            return

        self.pressed_keys.add(key)

        is_ctrl = HAS_PYNPUT and key in (Key.ctrl, Key.ctrl_l, Key.ctrl_r)
        is_shift = HAS_PYNPUT and key in (Key.shift, Key.shift_l, Key.shift_r)

        if is_ctrl:
            self.ctrl_pressed = True
            if self.shift_pressed:
                self.ctrl_shift_combo_active = True
                self.ctrl_shift_interrupted = False
            return
        elif is_shift:
            self.shift_pressed = True
            if self.ctrl_pressed:
                self.ctrl_shift_combo_active = True
                self.ctrl_shift_interrupted = False
            return
        else:
            if self.ctrl_pressed or self.shift_pressed or self.ctrl_shift_combo_active:
                self.ctrl_shift_interrupted = True

        if self.lang == 'EN':
            self.engine.reset_buffer()
            return

        if key == Key.backspace:
            if self.engine.raw_keys:
                action, backspace_count, text_to_insert = self.engine.process_backspace()
                if action == 'MODIFY':
                    threading.Thread(
                        target=self._inject_replacement_pynput,
                        args=(backspace_count, text_to_insert),
                        daemon=True
                    ).start()
                    return
            self.engine.reset_buffer()
            return

        if key in [Key.space, Key.enter, Key.tab, Key.left, Key.right, Key.up, Key.down, Key.esc]:
            self.engine.reset_buffer()
            return

        char = None
        if hasattr(key, 'char') and key.char:
            char = key.char

        if not char:
            return

        if not char.isalnum() and char != '_':
            self.engine.reset_buffer()
            return

        action, backspace_count, text_to_insert = self.engine.process_key(char)

        if action == 'MODIFY':
            threading.Thread(
                target=self._inject_replacement_pynput,
                args=(backspace_count + 1, text_to_insert),
                daemon=True
            ).start()

    def _on_release(self, key):
        if key in self.pressed_keys:
            self.pressed_keys.remove(key)

        is_ctrl = HAS_PYNPUT and key in (Key.ctrl, Key.ctrl_l, Key.ctrl_r)
        is_shift = HAS_PYNPUT and key in (Key.shift, Key.shift_l, Key.shift_r)

        if is_ctrl:
            self.ctrl_pressed = False
            if self.ctrl_shift_combo_active and not self.ctrl_shift_interrupted:
                self.toggle_language()
                self.ctrl_shift_combo_active = False
            if not self.ctrl_pressed and not self.shift_pressed:
                self.ctrl_shift_combo_active = False
                self.ctrl_shift_interrupted = False
        elif is_shift:
            self.shift_pressed = False
            if self.ctrl_shift_combo_active and not self.ctrl_shift_interrupted:
                self.toggle_language()
                self.ctrl_shift_combo_active = False
            if not self.ctrl_pressed and not self.shift_pressed:
                self.ctrl_shift_combo_active = False
                self.ctrl_shift_interrupted = False

    def _inject_replacement_pynput(self, backspace_count, text_to_insert):
        with self.pynput_inject_lock:
            self.is_injecting = True
            try:
                # Run after on_press returns; many Linux apps commit the raw
                # key just after the listener callback.
                time.sleep(self.pynput_pre_inject_delay)
                for _ in range(backspace_count):
                    self.keyboard_controller.press(Key.backspace)
                    time.sleep(self.pynput_key_delay)
                    self.keyboard_controller.release(Key.backspace)
                    time.sleep(self.pynput_key_delay)

                self.keyboard_controller.type(text_to_insert)
                time.sleep(self.pynput_key_delay)
            finally:
                self.is_injecting = False

    # ==================== SAFE EVDEV BACKEND ====================
    def _start_evdev(self):
        try:
            devices = [evdev.InputDevice(path) for path in evdev.list_devices()]
            kb_devices = []
            
            for dev in devices:
                caps = dev.capabilities()
                if ecodes.EV_KEY in caps:
                    keys = caps[ecodes.EV_KEY]
                    if ecodes.KEY_A in keys and ecodes.KEY_Z in keys:
                        name_lower = dev.name.lower()
                        if 'uinput' in name_lower or 'uniiki' in name_lower:
                            continue
                        kb_devices.append(dev)

            if not kb_devices:
                print("[Evdev] No physical keyboard devices found.")
                return False

            self.uinput_device = UInput(name="Uniiki Virtual Keyboard")
            
            for dev in kb_devices:
                try:
                    dev.grab()
                    self.grabbed_devices.append(dev)
                    t = threading.Thread(target=self._evdev_loop, args=(dev,), daemon=True)
                    t.start()
                    print(f"[Evdev] Successfully grabbed: {dev.name} ({dev.path})")
                except Exception as e:
                    print(f"[Evdev Warning] Could not grab {dev.name}: {e}")

            return len(self.grabbed_devices) > 0
        except Exception as e:
            print(f"[Evdev Init Error] {e}")
            return False

    def _evdev_loop(self, device):
        shift_pressed = False
        ctrl_pressed = False
        alt_pressed = False
        meta_pressed = False
        ctrl_shift_combo_active = False
        ctrl_shift_interrupted = False

        KEY_MAP = {
            ecodes.KEY_A: 'a', ecodes.KEY_B: 'b', ecodes.KEY_C: 'c', ecodes.KEY_D: 'd',
            ecodes.KEY_E: 'e', ecodes.KEY_F: 'f', ecodes.KEY_G: 'g', ecodes.KEY_H: 'h',
            ecodes.KEY_I: 'i', ecodes.KEY_J: 'j', ecodes.KEY_K: 'k', ecodes.KEY_L: 'l',
            ecodes.KEY_M: 'm', ecodes.KEY_N: 'n', ecodes.KEY_O: 'o', ecodes.KEY_P: 'p',
            ecodes.KEY_Q: 'q', ecodes.KEY_R: 'r', ecodes.KEY_S: 's', ecodes.KEY_T: 't',
            ecodes.KEY_U: 'u', ecodes.KEY_V: 'v', ecodes.KEY_W: 'w', ecodes.KEY_X: 'x',
            ecodes.KEY_Y: 'y', ecodes.KEY_Z: 'z'
        }

        try:
            for event in device.read_loop():
                if not self.is_running:
                    break
                if event.type == ecodes.EV_KEY:
                    key_event = categorize(event)
                    code = key_event.scancode
                    state = key_event.keystate

                    if code in [ecodes.KEY_LEFTSHIFT, ecodes.KEY_RIGHTSHIFT]:
                        if state == 1:
                            shift_pressed = True
                            if ctrl_pressed:
                                ctrl_shift_combo_active = True
                                ctrl_shift_interrupted = False
                        elif state == 0:
                            shift_pressed = False
                            if ctrl_shift_combo_active and not ctrl_shift_interrupted:
                                self.toggle_language()
                                ctrl_shift_combo_active = False
                            if not ctrl_pressed and not shift_pressed:
                                ctrl_shift_combo_active = False
                                ctrl_shift_interrupted = False
                        self._forward_event(event)
                        continue

                    if code in [ecodes.KEY_LEFTCTRL, ecodes.KEY_RIGHTCTRL]:
                        if state == 1:
                            ctrl_pressed = True
                            if shift_pressed:
                                ctrl_shift_combo_active = True
                                ctrl_shift_interrupted = False
                        elif state == 0:
                            ctrl_pressed = False
                            if ctrl_shift_combo_active and not ctrl_shift_interrupted:
                                self.toggle_language()
                                ctrl_shift_combo_active = False
                            if not ctrl_pressed and not shift_pressed:
                                ctrl_shift_combo_active = False
                                ctrl_shift_interrupted = False
                        self._forward_event(event)
                        continue

                    if code in [ecodes.KEY_LEFTALT, ecodes.KEY_RIGHTALT]:
                        alt_pressed = (state != 0)
                    if code in [ecodes.KEY_LEFTMETA, ecodes.KEY_RIGHTMETA]:
                        meta_pressed = (state != 0)

                    if state == 1 and code not in [ecodes.KEY_LEFTSHIFT, ecodes.KEY_RIGHTSHIFT, ecodes.KEY_LEFTCTRL, ecodes.KEY_RIGHTCTRL]:
                        if ctrl_pressed or shift_pressed or ctrl_shift_combo_active:
                            ctrl_shift_interrupted = True

                    if self.lang == 'EN':
                        self.engine.reset_buffer()
                        self._forward_event(event)
                        continue

                    if alt_pressed or meta_pressed or (ctrl_pressed and not shift_pressed):
                        self.engine.reset_buffer()
                        self._forward_event(event)
                        continue

                    if state == 1:
                        if code == ecodes.KEY_BACKSPACE:
                            if self.engine.raw_keys:
                                action, bcount, text_to_insert = self.engine.process_backspace()
                                if action == 'MODIFY':
                                    self.consumed_codes.add(code)
                                    self._evdev_inject_replacement(bcount, text_to_insert)
                                    continue
                            self.engine.reset_buffer()
                            self._forward_event(event)
                            continue

                        if code in [ecodes.KEY_SPACE, ecodes.KEY_ENTER, ecodes.KEY_TAB, ecodes.KEY_ESC]:
                            self.engine.reset_buffer()
                            self._forward_event(event)
                            continue

                        if code in KEY_MAP:
                            char = KEY_MAP[code]
                            if shift_pressed:
                                char = char.upper()

                            action, bcount, text_to_insert = self.engine.process_key(char)

                            if action == 'MODIFY':
                                self.consumed_codes.add(code)
                                self._evdev_inject_replacement(bcount, text_to_insert)
                            else:
                                self._forward_event(event)
                        else:
                            self._forward_event(event)
                    elif state == 0:
                        if code in self.consumed_codes:
                            self.consumed_codes.remove(code)
                        else:
                            self._forward_event(event)
        except Exception:
            pass

    def _forward_event(self, event):
        if self.uinput_device:
            self.uinput_device.write(event.type, event.code, event.value)
            self.uinput_device.syn()

    def _send_key_click(self, code):
        self.uinput_device.write(ecodes.EV_KEY, code, 1)
        self.uinput_device.syn()
        time.sleep(0.002)
        self.uinput_device.write(ecodes.EV_KEY, code, 0)
        self.uinput_device.syn()
        time.sleep(0.002)

    def _copy_to_clipboard(self, text):
        """Synchronously copies text to Linux system clipboard with GTK event loop iteration."""
        if self.gtk_clipboard:
            try:
                self.gtk_clipboard.set_text(text, -1)
                self.gtk_clipboard.store()
                # Synchronously flush GTK GDK event queue
                while Gtk.events_pending():
                    Gtk.main_iteration_do(False)
                return
            except Exception:
                pass

        env = os.environ.copy()
        try:
            p = subprocess.Popen(['wl-copy'], stdin=subprocess.PIPE, env=env, stderr=subprocess.DEVNULL)
            p.communicate(text.encode('utf-8'))
            return
        except Exception:
            pass

        try:
            p = subprocess.Popen(['xclip', '-selection', 'clipboard'], stdin=subprocess.PIPE, env=env, stderr=subprocess.DEVNULL)
            p.communicate(text.encode('utf-8'))
            return
        except Exception:
            pass

        if HAS_PYPERCLIP:
            try:
                pyperclip.copy(text)
            except Exception:
                pass

    def _evdev_inject_replacement(self, backspace_count, text_to_insert):
        if not self.uinput_device:
            return

        is_pure_ascii = all(ord(c) < 128 for c in text_to_insert)
        if not is_pure_ascii:
            self._copy_to_clipboard(text_to_insert)

        for _ in range(backspace_count):
            self._send_key_click(ecodes.KEY_BACKSPACE)

        if is_pure_ascii:
            for ch in text_to_insert:
                ch_lower = ch.lower()
                if ch_lower in CHAR_TO_EVDEV:
                    if ch.isupper():
                        self.uinput_device.write(ecodes.EV_KEY, ecodes.KEY_LEFTSHIFT, 1)
                        self.uinput_device.syn()
                    self._send_key_click(CHAR_TO_EVDEV[ch_lower])
                    if ch.isupper():
                        self.uinput_device.write(ecodes.EV_KEY, ecodes.KEY_LEFTSHIFT, 0)
                        self.uinput_device.syn()
        else:
            time.sleep(0.003)
            self.uinput_device.write(ecodes.EV_KEY, ecodes.KEY_LEFTSHIFT, 1)
            self.uinput_device.write(ecodes.EV_KEY, ecodes.KEY_INSERT, 1)
            self.uinput_device.syn()
            time.sleep(0.002)
            self.uinput_device.write(ecodes.EV_KEY, ecodes.KEY_INSERT, 0)
            self.uinput_device.write(ecodes.EV_KEY, ecodes.KEY_LEFTSHIFT, 0)
            self.uinput_device.syn()
