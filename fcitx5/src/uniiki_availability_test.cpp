#include "uniiki_engine.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

bool validUtf8(const std::string &text) {
    size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        size_t continuation = 0;
        if (first <= 0x7f) {
            ++index;
            continue;
        } else if ((first & 0xe0) == 0xc0 && first >= 0xc2) {
            continuation = 1;
        } else if ((first & 0xf0) == 0xe0) {
            continuation = 2;
        } else if ((first & 0xf8) == 0xf0 && first <= 0xf4) {
            continuation = 3;
        } else {
            return false;
        }
        if (index + continuation >= text.size()) {
            return false;
        }
        for (size_t offset = 1; offset <= continuation; ++offset) {
            if ((static_cast<unsigned char>(text[index + offset]) & 0xc0) !=
                0x80) {
                return false;
            }
        }
        index += continuation + 1;
    }
    return true;
}

enum class RuntimeEventType {
    KeyPress,
    KeyRelease,
    Backspace,
    Space,
    Enter,
    FocusIn,
    FocusOut,
    Reset,
    SurroundingUpdate,
    ContextDestroy,
};

struct RuntimeEvent {
    uint64_t id = 0;
    RuntimeEventType type = RuntimeEventType::KeyPress;
    char key = 0;
    uint64_t generation = 0;
    size_t dueTick = 0;
};

struct RuntimeModel {
    std::string raw;
    std::string rendered;
    std::deque<RuntimeEvent> queue;
    std::deque<RuntimeEvent> callbacks;
    std::unordered_set<uint64_t> appliedIds;
    uint64_t generation = 1;
    uint64_t desiredRevision = 0;
    uint64_t appliedRevision = 0;
    bool alive = true;
    bool focused = true;
    bool processing = false;
    bool renderInFlight = false;
    bool waitingForSurrounding = false;
    size_t staleCallbacks = 0;

    void clearComposition() {
        raw.clear();
        rendered.clear();
        queue.clear();
        processing = false;
        renderInFlight = false;
        waitingForSurrounding = false;
        desiredRevision = 0;
        appliedRevision = 0;
    }

    bool applyKey(const RuntimeEvent &event) {
        if (!alive || !focused || !appliedIds.insert(event.id).second) {
            return true;
        }
        processing = true;
        renderInFlight = true;
        raw.push_back(event.key);
        ++desiredRevision;
        rendered = fcitx::UniikiEngine::evaluateTelexForTest(raw);
        appliedRevision = desiredRevision;
        processing = false;
        renderInFlight = false;
        waitingForSurrounding = false;
        return !rendered.empty() &&
               fcitx::UniikiEngine::rawOwnershipInvariantForTest(raw);
    }

    bool dispatch(const RuntimeEvent &event, size_t tick) {
        switch (event.type) {
        case RuntimeEventType::KeyPress:
            return applyKey(event);
        case RuntimeEventType::KeyRelease:
            return true;
        case RuntimeEventType::Backspace:
            if (!raw.empty()) {
                raw.pop_back();
                rendered = raw.empty()
                               ? std::string()
                               : fcitx::UniikiEngine::evaluateTelexForTest(raw);
                ++desiredRevision;
                appliedRevision = desiredRevision;
            }
            return true;
        case RuntimeEventType::Space:
        case RuntimeEventType::Enter:
        case RuntimeEventType::Reset:
            clearComposition();
            return true;
        case RuntimeEventType::FocusIn:
            focused = true;
            return true;
        case RuntimeEventType::FocusOut:
            focused = false;
            ++generation;
            clearComposition();
            return true;
        case RuntimeEventType::ContextDestroy:
            alive = false;
            focused = false;
            ++generation;
            clearComposition();
            return true;
        case RuntimeEventType::SurroundingUpdate:
            if (!alive || event.generation != generation) {
                ++staleCallbacks;
                return true;
            }
            waitingForSurrounding = false;
            return true;
        }
        (void)tick;
        return false;
    }

    bool drain(size_t tick) {
        const size_t maxSteps = queue.size() * 4 + 16;
        size_t steps = 0;
        while (!queue.empty()) {
            if (++steps > maxSteps) {
                return false;
            }
            const auto event = queue.front();
            queue.pop_front();
            if (!dispatch(event, tick)) {
                return false;
            }
        }
        return !processing && !renderInFlight && !waitingForSurrounding;
    }
};

} // namespace

int main() {
    using Clock = std::chrono::steady_clock;
    using Micros = std::chrono::microseconds;

    constexpr size_t regressionIterations = 10000;
    auto regressionStart = Clock::now();
    for (size_t i = 0; i < regressionIterations; ++i) {
        const auto converted =
            fcitx::UniikiEngine::evaluateTelexForTest("khuyry");
        if (converted != "khuyry" ||
            !fcitx::UniikiEngine::rawOwnershipInvariantForTest("khuyry")) {
            std::cerr << "khuyry availability regression iteration=" << i
                      << " rendered=" << converted << '\n';
            return 1;
        }
    }
    const auto regressionMicros =
        std::chrono::duration_cast<Micros>(Clock::now() - regressionStart)
            .count();

    constexpr size_t fuzzIterations = 1000000;
    constexpr uint32_t fuzzSeed = 0x4B485559U;
    constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyzsfrxjw";
    constexpr size_t alphabetSize = sizeof(alphabet) - 1;
    std::mt19937 random(fuzzSeed);
    size_t maxInputMicros = 0;
    std::string slowestRaw;
    auto fuzzStart = Clock::now();
    for (size_t iteration = 0; iteration < fuzzIterations; ++iteration) {
        const size_t length = random() % 65;
        std::string raw;
        raw.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            raw.push_back(alphabet[random() % alphabetSize]);
        }

        const auto inputStart = Clock::now();
        const auto rendered =
            fcitx::UniikiEngine::evaluateTelexForTest(raw);
        const auto inputMicros = static_cast<size_t>(
            std::chrono::duration_cast<Micros>(Clock::now() - inputStart)
                .count());
        if (inputMicros > maxInputMicros) {
            maxInputMicros = inputMicros;
            slowestRaw = raw;
        }
        if ((!raw.empty() && rendered.empty()) || !validUtf8(rendered) ||
            rendered.size() > raw.size() * 3 ||
            !fcitx::UniikiEngine::rawOwnershipInvariantForTest(raw)) {
            std::cerr << "fuzz ownership/termination failure seed=" << fuzzSeed
                      << " iteration=" << iteration << " raw=" << raw
                      << " rendered=" << rendered << '\n';
            return 1;
        }
    }
    const auto fuzzMicros =
        std::chrono::duration_cast<Micros>(Clock::now() - fuzzStart).count();

    constexpr size_t recoveryIterations = 10000;
    for (size_t i = 0; i < recoveryIterations; ++i) {
        const auto visible =
            fcitx::UniikiEngine::simulateDirectForTest("khuyry abc");
        if (visible != "khuyry abc") {
            std::cerr << "post-failure recovery iteration=" << i
                      << " visible=" << visible << '\n';
            return 1;
        }
    }

    const std::vector<std::string> corpus = {
        "khuyry", "khuyru", "khuyruy", "khuyr", "ycxl", "lijchsjs",
        "dduaww", "delete", "deletee", "canaf", "oroooo", "dd", "cl",
    };
    constexpr size_t timingVariations = 10000;
    for (const auto &seed : corpus) {
        for (size_t variation = 0; variation < timingVariations; ++variation) {
            const auto rendered =
                fcitx::UniikiEngine::evaluateTelexForTest(seed);
            if ((!seed.empty() && rendered.empty()) || !validUtf8(rendered) ||
                !fcitx::UniikiEngine::rawOwnershipInvariantForTest(seed)) {
                std::cerr << "timing corpus failure raw=" << seed
                          << " variation=" << variation << '\n';
                return 1;
            }
        }
    }

    constexpr size_t runtimeSequences = 100000;
    constexpr uint32_t runtimeSeed = 0x51554555U;
    constexpr size_t delayChoices[] = {0, 1, 5, 10, 30, 100, 500};
    std::mt19937 runtimeRandom(runtimeSeed);
    size_t totalStaleCallbacks = 0;
    uint64_t nextEventId = 1;
    for (size_t sequence = 0; sequence < runtimeSequences; ++sequence) {
        RuntimeModel model;
        size_t tick = 0;
        const size_t eventCount = 8 + runtimeRandom() % 57;
        for (size_t eventIndex = 0; eventIndex < eventCount; ++eventIndex) {
            ++tick;
            RuntimeEvent event;
            event.id = nextEventId++;
            event.generation = model.generation;
            const auto selector = runtimeRandom() % 100;
            if (selector < 55) {
                event.type = RuntimeEventType::KeyPress;
                event.key = alphabet[runtimeRandom() % alphabetSize];
            } else if (selector < 62) {
                event.type = RuntimeEventType::KeyRelease;
            } else if (selector < 68) {
                event.type = RuntimeEventType::Backspace;
            } else if (selector < 73) {
                event.type = RuntimeEventType::Space;
            } else if (selector < 77) {
                event.type = RuntimeEventType::Enter;
            } else if (selector < 82) {
                event.type = RuntimeEventType::FocusOut;
            } else if (selector < 87) {
                event.type = RuntimeEventType::FocusIn;
                model.alive = true;
            } else if (selector < 91) {
                event.type = RuntimeEventType::Reset;
            } else if (selector < 96) {
                event.type = RuntimeEventType::SurroundingUpdate;
                event.dueTick = tick +
                    delayChoices[runtimeRandom() %
                                 (sizeof(delayChoices) / sizeof(size_t))];
                if ((runtimeRandom() % 3) == 0 && event.generation > 0) {
                    --event.generation;
                }
                model.callbacks.push_back(event);
                continue;
            } else {
                event.type = RuntimeEventType::ContextDestroy;
            }

            // Bursts queue several physical events before one bounded drain.
            model.queue.push_back(event);
            if ((runtimeRandom() % 5) == 0) {
                RuntimeEvent burst = event;
                burst.id = nextEventId++;
                burst.type = RuntimeEventType::KeyPress;
                burst.key = alphabet[runtimeRandom() % alphabetSize];
                model.queue.push_back(burst);
            }
            if (!model.drain(tick)) {
                std::cerr << "runtime queue/state failure seed=" << runtimeSeed
                          << " sequence=" << sequence
                          << " eventIndex=" << eventIndex << '\n';
                return 1;
            }

            while (!model.callbacks.empty() &&
                   model.callbacks.front().dueTick <= tick) {
                const auto callback = model.callbacks.front();
                model.callbacks.pop_front();
                if (!model.dispatch(callback, tick)) {
                    std::cerr << "runtime callback failure seed=" << runtimeSeed
                              << " sequence=" << sequence << '\n';
                    return 1;
                }
            }
        }

        while (!model.callbacks.empty()) {
            const auto callback = model.callbacks.front();
            model.callbacks.pop_front();
            if (!model.dispatch(callback, tick + 500)) {
                std::cerr << "runtime delayed callback failure seed="
                          << runtimeSeed << " sequence=" << sequence << '\n';
                return 1;
            }
        }
        totalStaleCallbacks += model.staleCallbacks;

        // Recovery property: after arbitrary failure/focus/context events,
        // a live focused context must always process the next literal abc.
        model.alive = true;
        model.focused = true;
        model.clearComposition();
        for (char key : std::string("abc")) {
            model.queue.push_back(
                {nextEventId++, RuntimeEventType::KeyPress, key,
                 model.generation, tick});
        }
        if (!model.drain(tick) || model.raw != "abc" ||
            model.rendered != "abc") {
            std::cerr << "runtime recovery failure seed=" << runtimeSeed
                      << " sequence=" << sequence
                      << " raw=" << model.raw
                      << " rendered=" << model.rendered << '\n';
            return 1;
        }
    }

    std::cout << "availability khuyry-iterations=" << regressionIterations
              << " khuyry-total-us=" << regressionMicros
              << " fuzz-seed=" << fuzzSeed
              << " fuzz-inputs=" << fuzzIterations
              << " fuzz-total-us=" << fuzzMicros
              << " max-input-us=" << maxInputMicros
              << " slowest-raw=" << slowestRaw
              << " recovery-iterations=" << recoveryIterations
              << " corpus-seeds=" << corpus.size()
              << " timing-variations=" << timingVariations
              << " runtime-seed=" << runtimeSeed
              << " runtime-sequences=" << runtimeSequences
              << " stale-callbacks-ignored=" << totalStaleCallbacks
              << " status=pass\n";
    return 0;
}
