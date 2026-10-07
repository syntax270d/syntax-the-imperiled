#include "animation.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
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

    char glyph(uint32_t pixel)
    {
        const auto red = (pixel >> 16) & 0xFF;
        const auto green = (pixel >> 8) & 0xFF;
        const auto blue = pixel & 0xFF;
        if (red > green * 1.5f)
            return 'X';
        if (red > 170 && green > 170 && blue > 170)
            return '@';
        if (green > 190 && blue > 100)
            return 'O';
        if (green > 130)
            return '#';
        if (green > 65)
            return '+';
        if (green > 15 || blue > 15)
            return '.';
        return ' ';
    }

    void print_pixel(uint32_t pixel)
    {
        if (pixel != 0)
        {
            const auto red = (pixel >> 16) & 0xFF;
            const auto green = (pixel >> 8) & 0xFF;
            const auto blue = pixel & 0xFF;
            std::cout << "\033[38;2;" << red << ';' << green << ';' << blue << 'm';
        }
        std::cout << glyph(pixel) << "\033[0m";
    }

    const char *mode_name(Mode mode)
    {
        switch (mode)
        {
        case Mode::Rain:
            return "RAIN";
        case Mode::Pulse:
            return "PULSE";
        case Mode::Glitch:
            return "GLITCH";
        case Mode::Charging:
            return "CHARGING";
        case Mode::Overcharge:
            return "OVERCHARGE";
        case Mode::PoweringDown:
            return "POWERING DOWN";
        }
        return "UNKNOWN";
    }

    void run_demo()
    {
        struct Segment
        {
            Milliseconds duration;
            float flex;
            const char *label;
        };
        const std::vector<Segment> segments = {
            {Milliseconds(8000), 0.0f, "Idle: multiple rain streams"},
            {Milliseconds(300), 0.30f, "Light flex: pulse"},
            {Milliseconds(1000), 0.0f, "Idle"},
            {Milliseconds(180), 0.55f, "Quick flex combo"},
            {Milliseconds(180), 0.0f, "Release"},
            {Milliseconds(180), 0.55f, "Quick flex combo"},
            {Milliseconds(180), 0.0f, "Release"},
            {Milliseconds(180), 0.55f, "Quick flex combo"},
            {Milliseconds(180), 0.0f, "Release"},
            {Milliseconds(180), 0.55f, "Quick flex combo"},
            {Milliseconds(180), 0.0f, "Release"},
            {Milliseconds(180), 0.55f, "Quick flex combo"},
            {Milliseconds(180), 0.0f, "Release"},
            {Milliseconds(180), 0.55f, "Quick flex combo"},
            {Milliseconds(180), 0.0f, "Release"},
            {Milliseconds(1800), 0.55f, "Medium flex: faster, denser rain"},
            {Milliseconds(1800), 0.0f, "Idle"},
            {Milliseconds(180), 0.55f, "First flex"},
            {Milliseconds(180), 0.0f, "Release"},
            {Milliseconds(700), 0.55f, "Second flex held: glitch"},
            {Milliseconds(2400), 0.0f, "Idle: glitch ends"},
            {Milliseconds(3400), 0.90f, "Tight hold: overcharge"},
            {Milliseconds(1200), 0.0f, "Release: power down"},
        };
        const auto demo_duration = std::accumulate(
            segments.begin(), segments.end(), Milliseconds(0),
            [](Milliseconds total, const Segment &segment) { return total + segment.duration; });

        Animator animator;
        const auto start = std::chrono::steady_clock::now();
        auto next_frame = start;
        while (true)
        {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<Milliseconds>(now - start);
            Milliseconds cycle_time(elapsed.count() % demo_duration.count());
            Milliseconds segment_start(0);
            const Segment *current = &segments.front();
            for (const auto &segment : segments)
            {
                if (cycle_time < segment_start + segment.duration)
                {
                    current = &segment;
                    break;
                }
                segment_start += segment.duration;
            }
            const Frame frame = animator.update(
                current->flex, std::chrono::duration_cast<Milliseconds>(elapsed));

            std::cout << "\033[2J\033[H"
                      << "Shared LED animation preview (Ctrl+C to quit)\n"
                      << "Demo: " << current->label
                      << " | mode: " << mode_name(frame.mode)
                      << " | flex: " << static_cast<int>(frame.flex * 100) << "%"
                      << " | glyph density: " << frame.glyph_density << "/5\n"
                      << "Flow: " << (frame.reversed ? "LED 143 -> LED 0 (into wearer)" :
                                                        "LED 0 -> LED 143 (toward strip end)")
                      << " | speed: " << frame.speed_min_leds_per_second
                      << "-" << frame.speed_max_leds_per_second << " LEDs/sec\n\n"
                      << "LED 0 [";
            for (const auto pixel : frame.pixels)
                print_pixel(pixel);
            std::cout << "] LED 143\n"
                      << std::flush;

            next_frame += Milliseconds(50);
            std::this_thread::sleep_until(next_frame);
        }
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

int main(int argc, char **argv)
{
    if (argc == 2 && std::string(argv[1]) == "--demo")
    {
        run_demo();
        return 0;
    }
    if (argc == 1)
        return run_tests();

    std::cerr << "Usage: " << argv[0] << " [--demo]\n";
    return 2;
}
