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
            ("tess", "te"),
            ("tesst", "test"),
            ("d", "d"),
            ("dddd", "đd"),
            ("datdang", "datdang"),
            ("datddang", "datdang"),
            ("dadang", "dadang"),
            ("add", "ad"),
            ("addd", "add"),
            ("aa", "â"),
            ("aaa", "aa"),
            ("ee", "ê"),
            ("eee", "ee"),
            ("oo", "ô"),
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
            ("as", "á"),
            ("ass", "a"),
            ("af", "à"),
            ("aff", "a"),
            ("ar", "ả"),
            ("arr", "a"),
            ("ax", "ã"),
            ("axx", "a"),
            ("aj", "ạ"),
            ("ajj", "a"),
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
            ("dduocw", "đươc"),
            ("dduocW", "đươc"),
            ("dduocww", "đuọcw"),
            ("dduocwW", "đuọcw"),
            ("wwork", "work"),
            ("duocw", "dươc"),
            ("duocwj", "dược"),
            ("dduocwj", "được"),
            ("chws", "chứ"),
            ("tw", "tư"),
            ("tws", "tứ"),
            ("nws", "nứ"),
            ("muoons", "muốn"),
            ("cuoons", "cuốn"),
            ("luoongs", "luống"),
            ("sstart", "start"),
            ("ffriend", "friend"),
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

    def test_work_typing_sequence(self):
        """Verify typing 'w w o r k' produces 'work' without extra letters."""
        self.engine.reset_buffer()
        word = ""
        for k in "wwork":
            action, bcount, insert = self.engine.process_key(k)
            if action == 'MODIFY':
                word = word[:-bcount] + insert
            elif action in ['APPEND', 'RESET']:
                word += insert
        self.assertEqual(word, "work")

    def test_duocw_variants(self):
        """Verify late 'w' hook logic for uo + coda words like duocw, duocwj, dduocwj, nuocw, truongwf."""
        variants = [
            ("duocw", "dươc"),
            ("duocwj", "dược"),
            ("dduocwj", "được"),
            ("nuocw", "nươc"),
            ("nuocws", "nước"),
            ("truongw", "trương"),
            ("truongwf", "trường"),
        ]
        for raw_keys, expected in variants:
            self.engine.reset_buffer()
            word = ""
            for k in raw_keys:
                action, bcount, insert = self.engine.process_key(k)
                if action == 'MODIFY':
                    word = word[:-bcount] + insert
                elif action in ['APPEND', 'RESET']:
                    word += insert
            self.assertEqual(word, expected, f"Failed for '{raw_keys}': got '{word}', expected '{expected}'")

    def test_english_words_with_escapes(self):
        """Verify typing English words with initial double keys produces expected English words."""
        cases = [
            ("wwork", "work"),
            ("sstart", "start"),
            ("ffriend", "friend"),
            ("data", "data"),
            ("apple", "apple"),
        ]
        for raw_keys, expected in cases:
            self.engine.reset_buffer()
            word = ""
            for k in raw_keys:
                action, bcount, insert = self.engine.process_key(k)
                if action == 'MODIFY':
                    if bcount > 0:
                        word = word[:-bcount] + insert
                    else:
                        word = word + insert
                elif action in ['APPEND', 'RESET']:
                    word += insert
            self.assertEqual(word, expected, f"Failed for '{raw_keys}': got '{word}', expected '{expected}'")

    def test_process_backspace_step_by_step(self):
        """Verify backspacing on 'duocw' step-by-step un-applies modifiers and characters properly."""
        self.engine.reset_buffer()
        word = ""
        for k in "duocw":
            action, bcount, insert = self.engine.process_key(k)
            if action == 'MODIFY':
                word = word[:-bcount] + insert
            elif action in ['APPEND', 'RESET']:
                word += insert
        self.assertEqual(word, "dươc")

        # Now press Backspace 1st time
        action, bcount, insert = self.engine.process_backspace()
        self.assertEqual(action, 'MODIFY')
        word = word[:-bcount] + insert
        self.assertEqual(word, "duoc")

        # Press Backspace 2nd time
        action, bcount, insert = self.engine.process_backspace()
        self.assertEqual(action, 'MODIFY')
        word = word[:-bcount] + insert
        self.assertEqual(word, "duo")

        # Press Backspace 3rd time
        action, bcount, insert = self.engine.process_backspace()
        self.assertEqual(action, 'MODIFY')
        word = word[:-bcount] + insert
        self.assertEqual(word, "du")

        # Press Backspace 4th time
        action, bcount, insert = self.engine.process_backspace()
        self.assertEqual(action, 'MODIFY')
        word = word[:-bcount] + insert
        self.assertEqual(word, "d")

        # Press Backspace 5th time
        action, bcount, insert = self.engine.process_backspace()
        self.assertEqual(action, 'MODIFY')
        word = word[:-bcount] + insert
        self.assertEqual(word, "")

        # Press Backspace 6th time (buffer empty)
        action, bcount, insert = self.engine.process_backspace()
        self.assertEqual(action, 'FORWARD')

    def test_backspace_on_tones(self):
        """Verify backspacing on 'chaof' step-by-step removes tone mark and then characters."""
        self.engine.reset_buffer()
        word = ""
        for k in "chaof":
            action, bcount, insert = self.engine.process_key(k)
            if action == 'MODIFY':
                word = word[:-bcount] + insert
            elif action in ['APPEND', 'RESET']:
                word += insert
        self.assertEqual(word, "chào")

        # BS 1: chào -> chua/chao
        action, bcount, insert = self.engine.process_backspace()
        word = word[:-bcount] + insert
        self.assertEqual(word, "chao")

        # BS 2: chao -> cha
        action, bcount, insert = self.engine.process_backspace()
        word = word[:-bcount] + insert
        self.assertEqual(word, "cha")

        # BS 3: cha -> ch
        action, bcount, insert = self.engine.process_backspace()
        word = word[:-bcount] + insert
        self.assertEqual(word, "ch")

        # BS 4: ch -> c
        action, bcount, insert = self.engine.process_backspace()
        word = word[:-bcount] + insert
        self.assertEqual(word, "c")

        # BS 5: c -> ""
        action, bcount, insert = self.engine.process_backspace()
        word = word[:-bcount] + insert
        self.assertEqual(word, "")

    def test_backspace_on_stroked_d(self):
        """Verify backspacing on stroked 'dd' step-by-step reverts to 'd' and then empty."""
        self.engine.reset_buffer()
        word = ""
        for k in "dd":
            action, bcount, insert = self.engine.process_key(k)
            if action == 'MODIFY':
                word = word[:-bcount] + insert
            elif action in ['APPEND', 'RESET']:
                word += insert
        self.assertEqual(word, "đ")

        # BS 1: đ -> d
        action, bcount, insert = self.engine.process_backspace()
        word = word[:-bcount] + insert
        self.assertEqual(word, "d")

        # BS 2: d -> ""
        action, bcount, insert = self.engine.process_backspace()
        word = word[:-bcount] + insert
        self.assertEqual(word, "")

    def test_backspace_on_cuire(self):
        """Verify backspacing on 'cuire' removes 'e' to leave 'cuir' instead of reverting to 'củi'."""
        self.engine.reset_buffer()
        word = ""
        for k in "cuire":
            action, bcount, insert = self.engine.process_key(k)
            if action == 'MODIFY':
                word = word[:-bcount] + insert if bcount > 0 else word + insert
            elif action in ['APPEND', 'RESET']:
                word += insert
        self.assertEqual(word, "cuire")

        # BS 1: cuire -> cuir
        action, bcount, insert = self.engine.process_backspace()
        self.assertEqual((action, bcount, insert), ('MODIFY', 1, ''))
        word = word[:-bcount] + insert if bcount > 0 else word + insert
        self.assertEqual(word, "cuir")

        # BS 2: cuir -> cui
        action, bcount, insert = self.engine.process_backspace()
        self.assertEqual((action, bcount, insert), ('MODIFY', 1, ''))
        word = word[:-bcount] + insert if bcount > 0 else word + insert
        self.assertEqual(word, "cui")

        # BS 3: cui -> cu
        action, bcount, insert = self.engine.process_backspace()
        self.assertEqual((action, bcount, insert), ('MODIFY', 1, ''))
        word = word[:-bcount] + insert if bcount > 0 else word + insert
        self.assertEqual(word, "cu")

        # BS 4: cu -> c
        action, bcount, insert = self.engine.process_backspace()
        self.assertEqual((action, bcount, insert), ('MODIFY', 1, ''))
        word = word[:-bcount] + insert if bcount > 0 else word + insert
        self.assertEqual(word, "c")

        # BS 5: c -> ""
        action, bcount, insert = self.engine.process_backspace()
        self.assertEqual((action, bcount, insert), ('MODIFY', 1, ''))
        word = word[:-bcount] + insert if bcount > 0 else word + insert
        self.assertEqual(word, "")

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

    def test_d_candidate_is_scoped_to_active_segment(self):
        steps = [
            ("d", 0, "d", "d"),
            ("da", 0, "da", "da"),
            ("dat", 0, "dat", "dat"),
            ("datd", 3, "d", "datd"),
            ("datdd", 3, "dd", "datd"),
            ("datdda", 3, "dda", "datda"),
            ("datddan", 3, "ddan", "datdan"),
            ("datddang", 3, "ddang", "datdang"),
        ]

        for raw, expected_start, expected_active, expected_rendered in steps:
            ranges = self.engine._split_raw_segments(list(raw))
            active_start, active_end = ranges[-1]
            self.assertEqual(active_start, expected_start)
            self.assertEqual(raw[active_start:active_end], expected_active)
            self.assertEqual(
                self.engine._evaluate_telex_sequence(list(raw)),
                expected_rendered,
            )

    def test_late_d_modifier_still_forms_stroked_d(self):
        self.assertEqual(
            self.engine._evaluate_telex_sequence(list("doodj")),
            "độ",
        )
        self.assertEqual(
            self.engine._evaluate_telex_sequence(list("dawjdng")),
            "đặng",
        )

    def test_punctuation_ends_word_without_disappearing(self):
        visible = ""
        for char in "https://ubuntu.com user@test.com":
            action, backspaces, insertion = self.engine.process_key(char)
            if action == 'MODIFY':
                visible = visible[:-backspaces] + insertion
            else:
                visible += insertion
        self.assertEqual(visible, "https://ubuntu.com user@test.com")

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
            ("dangd", "dangd"),
            ("trana", "trân"),
            ("tranaa", "trana"),
            ("trono", "trôn"),
            ("tronoo", "trono"),
            ("hoawcj", "hoặc"),
            ("ngoafi", "ngoài"),
            ("mama", "mâm"),
            ("mamaa", "mama"),
            ("mamaaa", "mamaa"),
            ("meme", "mêm"),
            ("memee", "meme"),
            ("como", "côm"),
            ("comoo", "como"),
            ("mams", "mám"),
            ("mamsa", "mấm"),
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
