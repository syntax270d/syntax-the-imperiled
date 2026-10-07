#include "animation.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using Milliseconds = std::chrono::milliseconds;
    using overclock::Animator;
    using overclock::Frame;
    using overclock::Mode;

    void expect(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void test_adc_normalization()
    {
        expect(overclock::normalize_adc(0) == 0.0f, "ADC readings below calibration range clamp to zero");
        expect(overclock::normalize_adc(500) == 0.5f, "ADC midpoint normalizes to 0.5");
        expect(overclock::normalize_adc(1023) == 1.0f, "ADC readings above calibration range clamp to one");
    }

    void test_idle_has_multiple_streams()
    {
        Animator animator;
        const Frame frame = animator.update(0.0f, Milliseconds(0));
        const auto lit_pixels = std::count_if(
            frame.pixels.begin(), frame.pixels.end(),
            [](uint32_t pixel) { return pixel != 0; });

        expect(frame.mode == Mode::Rain, "unflexed input stays in rain mode");
        expect(frame.glyph_density == 0, "idle starts at baseline glyph density");
        expect(lit_pixels >= overclock::STREAM_COUNT * 10,
               "idle frame renders visible trails for multiple streams");
        expect(frame.speed_min_leds_per_second > 0.0f &&
                   frame.speed_max_leds_per_second > frame.speed_min_leds_per_second,
               "streams expose a spread of baseline speeds");
    }

    void test_pulse_duration()
    {
        Animator animator;
        animator.update(0.0f, Milliseconds(0));
        const Frame pulse = animator.update(0.3f, Milliseconds(100));
        const Frame expired = animator.update(0.0f, Milliseconds(300));

        expect(pulse.mode == Mode::Pulse, "threshold crossing starts pulse");
        expect(expired.mode == Mode::Rain, "pulse expires after its duration");
    }

    void test_repeated_flex_increases_density()
    {
        Animator animator;
        animator.update(0.0f, Milliseconds(0));
        for (int flex = 0; flex < 6; ++flex)
        {
            const auto onset = Milliseconds(10 + flex * 200);
            animator.update(0.55f, onset);
            animator.update(0.0f, onset + Milliseconds(80));
        }

        const Frame frame = animator.update(0.0f, Milliseconds(1300));
        expect(frame.glyph_density == 5, "six flexes build and cap glyph density at five");
    }

    void test_double_flex_glitch()
    {
        Animator animator;
        animator.update(0.0f, Milliseconds(0));
        animator.update(0.55f, Milliseconds(10));
        animator.update(0.0f, Milliseconds(100));
        animator.update(0.55f, Milliseconds(300));
        const Frame glitch = animator.update(0.55f, Milliseconds(800));
        const Frame ended = animator.update(0.0f, Milliseconds(2900));

        expect(glitch.mode == Mode::Glitch, "held second flex triggers glitch");
        expect(std::any_of(glitch.pixels.begin(), glitch.pixels.end(),
                           [](uint32_t pixel) { return pixel == 0xFF0000; }),
               "glitch frame contains crimson pixels");
        expect(ended.mode == Mode::Rain, "glitch ends after its duration");
    }

    void test_overcharge_and_power_down()
    {
        Animator animator;
        animator.update(0.0f, Milliseconds(0));
        animator.update(0.9f, Milliseconds(10));
        const Frame charged = animator.update(0.9f, Milliseconds(3010));
        const Frame powering_down = animator.update(0.0f, Milliseconds(3110));
        const Frame powered_down = animator.update(0.0f, Milliseconds(3800));

        expect(charged.mode == Mode::Overcharge && charged.charge == 1.0f,
               "three-second tight hold completes overcharge");
        expect(charged.reversed, "overcharge reverses strip flow");
        expect(powering_down.mode == Mode::PoweringDown,
               "releasing after charge begins power down");
        expect(powered_down.mode == Mode::Rain && powered_down.charge == 0.0f,
               "power down returns to normal rain");
    }

    void test_early_release_cancels_charge()
    {
        Animator animator;
        animator.update(0.0f, Milliseconds(0));
        animator.update(0.9f, Milliseconds(10));
        const Frame canceled = animator.update(0.0f, Milliseconds(1510));

        expect(canceled.mode == Mode::PoweringDown && canceled.charge < 1.0f,
               "early release cancels and fades the incomplete charge");
    }

    int run_tests()
    {
        const std::vector<std::pair<const char *, void (*)()>> tests = {
            {"ADC normalization", test_adc_normalization},
            {"multiple idle streams", test_idle_has_multiple_streams},
            {"pulse duration", test_pulse_duration},
            {"flex density", test_repeated_flex_increases_density},
            {"double-flex glitch", test_double_flex_glitch},
            {"overcharge and power down", test_overcharge_and_power_down},
            {"early release cancellation", test_early_release_cancels_charge},
        };

        int failures = 0;
        for (const auto &test : tests)
        {
            try
            {
                test.second();
                std::cout << "[PASS] " << test.first << '\n';
            }
            catch (const std::exception &error)
            {
                ++failures;
                std::cerr << "[FAIL] " << test.first << ": " << error.what() << '\n';
            }
        }

        std::cout << tests.size() - failures << "/" << tests.size() << " tests passed\n";
        return failures == 0 ? 0 : 1;
    }
}

int main()
{
    return run_tests();
}
