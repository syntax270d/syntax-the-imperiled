#include "animation.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

namespace
{
    using Milliseconds = std::chrono::milliseconds;

    struct InputSegment
    {
        Milliseconds duration;
        float flex;
        const char *label;
    };

    const std::vector<InputSegment> DEMO = {
        {Milliseconds(14000), 0.0f, "Idle: baseline rain"},
        {Milliseconds(300), 0.30f, "Light flex: pulse"},
        {Milliseconds(1000), 0.0f, "Idle: baseline rain"},
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
        {Milliseconds(2200), 0.55f, "Medium flex: faster, denser rain"},
        {Milliseconds(1800), 0.0f, "Idle: rain returns to baseline"},
        {Milliseconds(180), 0.55f, "First flex"},
        {Milliseconds(180), 0.0f, "Release"},
        {Milliseconds(700), 0.55f, "Second flex held: glitch"},
        {Milliseconds(2400), 0.0f, "Idle: glitch ends"},
        {Milliseconds(3400), 0.90f, "Tight hold: overcharge"},
        {Milliseconds(1200), 0.0f, "Release: power down"},
    };

    const char *mode_name(overclock::Mode mode)
    {
        switch (mode)
        {
        case overclock::Mode::Rain:
            return "RAIN";
        case overclock::Mode::Pulse:
            return "PULSE";
        case overclock::Mode::Glitch:
            return "GLITCH";
        case overclock::Mode::Charging:
            return "CHARGING";
        case overclock::Mode::Overcharge:
            return "OVERCHARGE";
        case overclock::Mode::PoweringDown:
            return "POWERING DOWN";
        }
        return "UNKNOWN";
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

    void run()
    {
        const Milliseconds demo_duration = std::accumulate(
            DEMO.begin(), DEMO.end(), Milliseconds(0),
            [](Milliseconds total, const InputSegment &segment)
            { return total + segment.duration; });

        overclock::Animator animator;
        const auto start = std::chrono::steady_clock::now();
        auto next_frame = start;
        while (true)
        {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<Milliseconds>(now - start);
            const Milliseconds cycle_time(elapsed.count() % demo_duration.count());
            Milliseconds segment_start(0);
            const InputSegment *current = &DEMO.front();
            for (const auto &segment : DEMO)
            {
                if (cycle_time < segment_start + segment.duration)
                {
                    current = &segment;
                    break;
                }
                segment_start += segment.duration;
            }

            const overclock::Frame frame = animator.update(current->flex, elapsed);
            std::cout << "\033[2J\033[H"
                      << "Shared LED strip preview (Ctrl+C to quit)\n"
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
}

int main()
{
    run();
    return 0;
}
