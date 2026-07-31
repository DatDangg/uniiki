import unittest
import sys
import os

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.engine import VietnameseEngine

class TestVietnameseEngine(unittest.TestCase):
    def setUp(self):
        self.engine = VietnameseEngine(mode='telex', modern_tone=True)

    def _simulate_suppressed_backend(self, raw_text):
        self.engine.reset_buffer()
        visible_text = ""
        for char in raw_text:
            if not char.isalnum() and char != "_":
                self.engine.reset_buffer()
                visible_text += char
                continue

            action, bcount, insert = self.engine.process_key(char)
            if action == 'MODIFY':
                visible_text = visible_text[:-bcount] + insert
            elif action in ['APPEND', 'RESET']:
                visible_text += insert
        return visible_text

    def _simulate_pynput_backend(self, raw_text):
        self.engine.reset_buffer()
        visible_text = ""
        for char in raw_text:
            visible_text += char
            if not char.isalnum() and char != "_":
                self.engine.reset_buffer()
                continue

            action, bcount, insert = self.engine.process_key(char)
            if action == 'MODIFY':
                visible_text = visible_text[:-(bcount + 1)] + insert
        return visible_text

    def test_telex_double_key_escaping(self):
        tests = [
            ("tes", "té"),
            ("tess", "tes"),
            ("tesst", "test"),
            ("aaa", "aa"),
            ("eee", "ee"),
            ("ooo", "oo"),
            ("dd", "đ"),
            ("ddd", "dd"),
            ("ddu", "đu"),
            ("dduo", "đuo"),
            ("dduow", "đươ"),
            ("dduowngf", "đường"),
            ("dduowcj", "được"),
            ("w", "ư"),
            ("ww", "w"),
            ("uw", "ư"),
            ("uww", "uw"),
            ("ow", "ơ"),
            ("oww", "ow"),
            ("aw", "ă"),
            ("aww", "aw"),
            ("wW", "w"),
            ("Ww", "W"),
            ("W", "Ư"),
            ("WW", "W"),
            ("c", "c"),
            ("ch", "ch"),
            ("chu", "chu"),
            ("chua", "chua"),
            ("chuaw", "chưa"),
            ("chuaww", "chuaw"),
            ("chuw", "chư"),
            ("muaw", "mưa"),
            ("thuaw", "thưa"),
            ("duaw", "dưa"),
            ("tuowngf", "tường"),
            ("nguowif", "người"),
            ("duowjc", "dược"),
            ("dduowjc", "được"),
            ("nuowcs", "nước"),
            ("truowngf", "trường"),
            ("thuowngf", "thường"),
            ("dduocw", "đuơc"),
            ("dduocW", "đuơc"),
            ("dduocww", "đuọcw"),
            ("dduocwW", "đuọcw"),
            ("dduocwjw", "đuọcw"),
            ("dduocwjW", "đuọcw"),
            ("dduowcjw", "đuọcw"),
            ("dduowcjW", "đuọcw"),
            ("pre", "pre"),
            ("google", "google"),
            ("gooogle", "google"),
            ("sangs", "sáng"),
            ("dduwoongf", "đường"),
            ("vieetj", "việt"),
            ("giuwx", "giữ"),
            ("JavaScript", "JavaScript"),
            ("TypeScript", "TypeScript"),
        ]
        
        for raw_keys, expected in tests:
            self.engine.reset_buffer()
            word = ""
            for k in raw_keys:
                action, bcount, insert = self.engine.process_key(k)
                if action == 'MODIFY':
                    word = word[:-bcount] + insert
                elif action in ['APPEND', 'RESET']:
                    word += insert
            final_word = self.engine.get_current_word()
            self.assertEqual(final_word, expected, f"Failed for {raw_keys}: got {final_word}, expected {expected}")

    def test_raw_backspace_reconverts_word(self):
        raw_keys = list("chuaw")
        self.assertEqual(self.engine._evaluate_telex_sequence(raw_keys), "chưa")
        raw_keys.pop()
        self.assertEqual(self.engine._evaluate_telex_sequence(raw_keys), "chua")

    def test_w_escape_after_retyping(self):
        raw_keys = list("chua")
        self.assertEqual(self.engine._evaluate_telex_sequence(raw_keys), "chua")
        raw_keys.append("w")
        self.assertEqual(self.engine._evaluate_telex_sequence(raw_keys), "chưa")
        raw_keys.append("w")
        self.assertEqual(self.engine._evaluate_telex_sequence(raw_keys), "chuaw")

    def test_sentence_visible_output_with_word_resets(self):
        raw_text = "xin chaof, tooi ddang kieemr tra booj gox tieengs vieetj treen ubuntu"
        expected = "xin chào, tôi đang kiểm tra bộ gõ tiếng việt trên ubuntu"

        self.assertEqual(self._simulate_suppressed_backend(raw_text), expected)

    def test_sentence_visible_output_with_pynput_backend(self):
        tests = [
            (
                "xin chaof tooi ddang kieemr tra booj gox tieengs vieetj treen ubuntu",
                "xin chào tôi đang kiểm tra bộ gõ tiếng việt trên ubuntu",
            ),
            (
                "hom nay thowif tieets khas ddepj nhieetj ddooj khoangr 30 ddooj",
                "hom nay thời tiết khá đẹp nhiệt độ khoảng 30 độ",
            ),
        ]

        for raw_text, expected in tests:
            self.assertEqual(self._simulate_pynput_backend(raw_text), expected)

    def test_late_telex_mark_keys(self):
        tests = [
            ("chafo", "chào"),
            ("toio", "tôi"),
            ("bojo", "bộ"),
            ("dangd", "đang"),
            ("trana", "trân"),
            ("trono", "trôn"),
            ("hoawcj", "hoặc"),
            ("ngoafi", "ngoài"),
        ]

        for raw_keys, expected in tests:
            self.engine.reset_buffer()
            word = ""
            for k in raw_keys:
                action, bcount, insert = self.engine.process_key(k)
                if action == 'MODIFY':
                    word = word[:-bcount] + insert
                elif action in ['APPEND', 'RESET']:
                    word += insert
            self.assertEqual(word, expected, f"Failed for {raw_keys}: got {word}, expected {expected}")

if __name__ == '__main__':
    unittest.main()
