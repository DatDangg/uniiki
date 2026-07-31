# Vietnamese IME No-Preedit Test Report

Date: 2026-07-31

## Result

**GTK PASS / FULL APPLICATION MATRIX NOT VERIFIED**

After the fixes, three consecutive GTK end-to-end runs passed 100/100 cases
with zero correctness failures, crashes or freezes. The full specification
cannot receive an unconditional PASS until the blocked and unavailable
application environments are run.

## Environment

- OS: Ubuntu 26.04 LTS
- Kernel: 7.0.0-28-generic
- Session: GNOME Wayland with Xwayland
- Fcitx: 5.1.19
- Uniiki library SHA-256:
  `9224deadfb6b323e0fe13fc65e2b78361be8a56752cb2b91510b6de6b5c7e59b`
- Native integration target: GTK 3.24 TextView, X11, `im-fcitx5`

## Environment Matrix

| Environment | Status | Notes |
|---|---:|---|
| GTK text entry | **PASS** | Three consecutive 100/100 Fcitx/Uniiki runs |
| Chrome | NOT RUN | Not installed |
| Firefox | BLOCKED | Firefox Snap could not open the test X11 display |
| Cốc Cốc/Chromium substitute | BLOCKED | Test window opened, but it did not create a Fcitx context; raw `minhf` was received |
| ChatGPT | NOT RUN | Browser input context could not be automated reliably |
| Google Search | NOT RUN | Browser input context could not be automated reliably |
| Zalo Web | NOT RUN | Browser input context could not be automated reliably |
| VSCode 1.130 / Electron 42 | BLOCKED | Isolated X11 profile opened, but it did not create a Fcitx context; raw `minhf` was saved |
| Terminal | NOT RUN | No testable terminal executable was available |
| Qt text entry | NOT RUN | Qt runtime exists, but no test application/dev target was available |

`BLOCKED` and `NOT RUN` are not counted as PASS.

## Post-Fix Results

- Native probe: 105/105 passed
- GTK run 1: 100/100 passed
- GTK run 2: 100/100 passed
- GTK run 3: 100/100 passed
- Total GTK characters typed: 9,282
- Total GTK words checked: 1,686
- Fast sentence: 60/60 repetitions passed
- Stress text: 3/3 runs passed
- Backspace: 3/3 runs passed
- Mixed English/Vietnamese: 3/3 runs passed
- Copy/paste then continue: 3/3 runs passed
- Enter once: 3/3 runs passed
- Crash count: 0
- Freeze count: 0
- Raw Telex leak count: 0
- Missing character count: 0
- Duplicate character count: 0
- Wrong tone count: 0
- Wrong Unicode count: 0
- Cursor/order corruption count: 0
- Enter failure count: 0
- Explicit live case: `tes tess tesst` -> `té tes test`
- Rapid Backspace at 1 ms/key: passed, final text empty

## Pre-Fix Native Probe

- Cases: 105
- Raw input bytes: 3,480
- Failures: 5
- Basic words, tones, Unicode table, long sentence, fast-tone,
  Backspace simulation and Enter simulation passed.
- Stress text failed four times.
- Mixed English/Vietnamese failed once.

Native conversion failures included:

- `giuwx` -> `gĩư` instead of `giữ`
- `Windows` -> `ưindớ`
- `Google` -> `Gôgle`
- `raw` -> `ră`
- `version` -> `véion`

## Pre-Fix GTK End-to-End Statistics

- Test cases: 100
- Characters typed: 3,094
- Words checked: 562
- Passed cases: 78
- Failed cases: 22
- Crash count: 0
- Freeze count: 0
- Enter failure count: 0
- Paste/continue failure count: 0
- Fast sentence failures: 19/20
- Stress failures: 1/1
- Backspace failures: 1/1
- Mixed-language failures: 1/1
- Character multiset deficits across string failures: 40
- Character multiset extras across string failures: 26
- Raw Telex leak count in the connected GTK suite: 0 observed
- Cursor/order corruption cases: at least 19
- Wrong tone/Unicode cases: present; exact character attribution is
  confounded by the same ordering race

## Pre-Fix Group Results

| Test group | Passed | Failed |
|---|---:|---:|
| Basic Vietnamese words | 14 | 0 |
| Tone keys | 18 | 0 |
| Vietnamese Unicode characters | 37 | 0 |
| Long sentence, slow | 1 | 0 |
| Long sentence, fast, 20 repeats | 1 | 19 |
| Stress text, over 1,000 characters | 0 | 1 |
| Fast tone input | 5 | 0 |
| Backspace sequence | 0 | 1 |
| Mixed English/Vietnamese | 0 | 1 |
| Copy/paste then continue typing | 1 | 0 |
| Enter once | 1 | 0 |

## Pre-Fix Reproduction 1: Fast Commit Ordering

1. Use a GTK 3 text entry with Fcitx and Uniiki active.
2. Type quickly:
   `Hoom nay minhf ddang kieemr tra booj gox tieengs Vieetj treen Linux.`
3. Repeat 20 times.

Expected:

`Hôm nay mình đang kiểm tra bộ gõ tiếng Việt trên Linux.`

Observed examples:

- `Hôm nay mình đang kiểm tra bộ gõti ếng Việt trên Linux.`
- `Hômn ay mình đang kiểm trab ộ gõ tiếng Việt trên Linux.`
- `Hôm nay mình đang kiểm tra bộg õ tiếng Việt trên Linux.`
- `Hôm nay mìnhdđang kiểm tra bộ gõ tiếng Việtt rên Linux.`

Likely cause: committed replacement text and forwarded boundary keys use
different asynchronous paths, so spaces and later commits can be observed out
of order. Immediate replacement completion also permits a new replacement
before the client has applied the previous one.

## Pre-Fix Reproduction 2: Backspace

1. Type `minhf`.
2. Press Backspace four times.

Expected:

`mình` -> `mìn` -> `mì` -> `m` -> empty

Observed:

`mình` -> `minh` -> `min` -> `mi` -> `m`

Cause: Backspace removes a raw Telex key and reevaluates the raw buffer instead
of removing one visible committed Unicode character.

## Pre-Fix Reproduction 3: Mixed Language

Type:

`Today minhf sex tesst Vietnamese IME version 2.`

Expected:

`Today mình sẽ test Vietnamese IME version 2.`

Observed:

`Today mình sẽ test Vietnamese IME véion 2.`

The protected English vocabulary and syllable rejection rules do not cover
normal mixed-language text consistently.

## No-Preedit Observation

The addon source does not call Fcitx preedit update APIs. The connected GTK
buffer contained committed text only, and no raw tone keys were committed in
the passing conversion groups. Visual preedit rendering was not independently
captured by screenshot automation.

## Tests Not Completed

- Cursor editing to correct every word in the provided sentence
- Repeated application switching
- Repeated VN/EN mode toggling
- Ten-minute random editing
- Chrome, Firefox, ChatGPT, Google Search, Zalo Web, VSCode, Terminal and Qt
  end-to-end runs

These were not promoted to PASS because the required application input
contexts were unavailable or not connected to Fcitx in the isolated test
profiles. GTK is now passing, but the full application matrix remains
unverified.
