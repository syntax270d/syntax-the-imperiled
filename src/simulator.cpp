#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;
    using Milliseconds = std::chrono::milliseconds;

    constexpr int LED_COUNT = 144;
    constexpr float PULSE_THRESHOLD = 0.20f;
    constexpr float FLEX_THRESHOLD = 0.45f;
    constexpr float FLEX_RELEASE_THRESHOLD = 0.40f;
    constexpr float OVERCHARGE_THRESHOLD = 0.80f;
    constexpr float OVERCHARGE_RELEASE_THRESHOLD = 0.75f;
    constexpr auto PULSE_DURATION = Milliseconds(180);
    constexpr auto PULSE_COOLDOWN = Milliseconds(500);
    constexpr auto DOUBLE_FLEX_WINDOW = Milliseconds(1500);
    constexpr auto DOUBLE_FLEX_HOLD = Milliseconds(500);
    constexpr auto GLITCH_DURATION = Milliseconds(2000);
    constexpr auto OVERCHARGE_HOLD = Milliseconds(3000);
    constexpr auto POWER_DOWN_DURATION = Milliseconds(600);
    constexpr auto FLEX_COMBO_WINDOW = Milliseconds(3000);
    constexpr auto FRAME_DURATION = Milliseconds(50);

    struct InputSegment
    {
        Milliseconds duration;
        float flex;
    };

    const std::vector<InputSegment> DEMO = {
        {Milliseconds(800), 0.0f},
        {Milliseconds(220), 0.30f},
        {Milliseconds(550), 0.0f},
        {Milliseconds(180), 0.55f},
        {Milliseconds(180), 0.0f},
        {Milliseconds(650), 0.55f},
        {Milliseconds(2200), 0.0f},
        {Milliseconds(180), 0.55f},
        {Milliseconds(180), 0.0f},
        {Milliseconds(650), 0.55f},
        {Milliseconds(1600), 0.0f},
        {Milliseconds(3400), 0.90f},
        {Milliseconds(900), 0.0f},
        {Milliseconds(700), 0.0f},
    };

    float simulated_flex(Milliseconds cycle_time, std::mt19937 &random)
    {
        Milliseconds segment_start(0);
        float flex = 0.0f;
        for (const auto &segment : DEMO)
        {
            if (cycle_time < segment_start + segment.duration)
            {
                flex = segment.flex;
                break;
            }
            segment_start += segment.duration;
        }

        std::uniform_real_distribution<float> jitter(-0.008f, 0.008f);
        return std::clamp(flex + jitter(random), 0.0f, 1.0f);
    }

    char pixel_glyph(uint32_t pixel)
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
        std::cout << pixel_glyph(pixel) << "\033[0m";
    }
}

int main()
{
    std::mt19937 random(std::random_device{}());
    std::array<uint32_t, LED_COUNT> pixels{};
    std::vector<Clock::time_point> recent_flexes;
    const Milliseconds demo_duration = std::accumulate(
        DEMO.begin(), DEMO.end(), Milliseconds(0),
        [](Milliseconds total, const InputSegment &segment)
        { return total + segment.duration; });

    float rain_head = 0.0f;
    float pulse_head = 0.0f;
    float charge_progress = 0.0f;
    float power_down_charge = 0.0f;
    bool pulse_threshold_active = false;
    bool gesture_active = false;
    bool has_first_flex = false;
    bool waiting_for_double_flex_hold = false;
    bool charge_tracking = false;
    bool powering_down = false;
    bool pulse_active = false;
    Clock::time_point first_flex_time{};
    Clock::time_point double_flex_hold_start{};
    Clock::time_point charge_start{};
    Clock::time_point power_down_start{};
    Clock::time_point pulse_start{};
    Clock::time_point pulse_until{};
    Clock::time_point pulse_cooldown_until{};
    Clock::time_point glitch_start{};
    Clock::time_point glitch_until{};
    const auto start = Clock::now();
    auto next_frame = start;

    while (true)
    {
        const auto now = Clock::now();
        const auto runtime = std::chrono::duration_cast<Milliseconds>(now - start);
        const Milliseconds cycle_time(runtime.count() % demo_duration.count());
        const float flex_pct = simulated_flex(cycle_time, random);

        if (flex_pct >= PULSE_THRESHOLD && !pulse_threshold_active)
        {
            pulse_threshold_active = true;
            if (now >= pulse_cooldown_until)
            {
                pulse_active = true;
                pulse_head = rain_head;
                pulse_start = now;
                pulse_until = now + PULSE_DURATION;
                pulse_cooldown_until = now + PULSE_COOLDOWN;
            }
        }
        else if (flex_pct < PULSE_THRESHOLD - 0.05f)
        {
            pulse_threshold_active = false;
        }

        if (flex_pct >= FLEX_THRESHOLD && !gesture_active)
        {
            gesture_active = true;
            while (!recent_flexes.empty() && now - recent_flexes.front() > FLEX_COMBO_WINDOW)
                recent_flexes.erase(recent_flexes.begin());
            recent_flexes.push_back(now);

            if (has_first_flex && now - first_flex_time <= DOUBLE_FLEX_WINDOW)
            {
                waiting_for_double_flex_hold = true;
                double_flex_hold_start = now;
                has_first_flex = false;
            }
            else
            {
                has_first_flex = true;
                first_flex_time = now;
            }
        }
        else if (flex_pct < FLEX_RELEASE_THRESHOLD && gesture_active)
        {
            gesture_active = false;
            if (waiting_for_double_flex_hold)
            {
                waiting_for_double_flex_hold = false;
                has_first_flex = false;
            }
        }

        if (has_first_flex && now - first_flex_time > DOUBLE_FLEX_WINDOW)
            has_first_flex = false;

        if (waiting_for_double_flex_hold && flex_pct >= OVERCHARGE_THRESHOLD)
        {
            waiting_for_double_flex_hold = false;
            has_first_flex = false;
        }
        else if (waiting_for_double_flex_hold &&
                 now - double_flex_hold_start >= DOUBLE_FLEX_HOLD)
        {
            glitch_start = now;
            glitch_until = now + GLITCH_DURATION;
            waiting_for_double_flex_hold = false;
            has_first_flex = false;
        }

        const bool tightly_flexed = charge_tracking
                                        ? flex_pct >= OVERCHARGE_RELEASE_THRESHOLD
                                        : flex_pct >= OVERCHARGE_THRESHOLD;
        if (!powering_down && tightly_flexed)
        {
            if (!charge_tracking)
            {
                charge_tracking = true;
                charge_start = now;
            }
            charge_progress = std::min(
                1.0f,
                std::chrono::duration<float>(now - charge_start).count() /
                    std::chrono::duration<float>(OVERCHARGE_HOLD).count());
        }
        else if (!powering_down && charge_tracking)
        {
            powering_down = true;
            power_down_start = now;
            power_down_charge = charge_progress;
            charge_tracking = false;
        }

        if (powering_down)
        {
            const auto elapsed = now - power_down_start;
            const float fade = std::max(
                0.0f,
                1.0f - std::chrono::duration<float>(elapsed).count() /
                    std::chrono::duration<float>(POWER_DOWN_DURATION).count());
            charge_progress = power_down_charge * fade;
            if (elapsed >= POWER_DOWN_DURATION)
            {
                powering_down = false;
                charge_progress = 0.0f;
            }
        }
        else if (!charge_tracking)
        {
            charge_progress = 0.0f;
        }

        if (pulse_active && now >= pulse_until)
            pulse_active = false;
        while (!recent_flexes.empty() && now - recent_flexes.front() > FLEX_COMBO_WINDOW)
            recent_flexes.erase(recent_flexes.begin());

        const int density = std::min(5, std::max(0, static_cast<int>(recent_flexes.size()) - 1));
        const float trail_length = 15.0f + density * 5.0f;
        const int glyph_spacing = std::max(4, 10 - density);
        const float elapsed_seconds = std::chrono::duration<float>(now - start).count();
        float animation_speed = 0.2f + flex_pct * 0.8f;
        const float brightness = 40.0f + flex_pct * 215.0f;
        if (charge_progress > 0.0f)
        {
            animation_speed = 1.0f + charge_progress * 11.0f;
            rain_head -= animation_speed;
            if (rain_head < 0.0f)
                rain_head += LED_COUNT;
        }
        else
        {
            rain_head += animation_speed;
            if (rain_head >= LED_COUNT)
                rain_head -= LED_COUNT;
        }

        const bool glitch_active = now < glitch_until && charge_progress == 0.0f;
        const float pulse_elapsed = std::chrono::duration<float>(now - pulse_start).count();
        const float current_pulse_head = std::fmod(
            pulse_head + pulse_elapsed * 40.0f, static_cast<float>(LED_COUNT));

        for (int i = 0; i < LED_COUNT; ++i)
        {
            if (glitch_active)
            {
                const auto glitch_elapsed =
                    std::chrono::duration_cast<Milliseconds>(now - glitch_start).count();
                pixels[i] = ((i * 17 + glitch_elapsed / 40) % 10 < 3) ? 0xFF0000 : 0;
                continue;
            }

            const float distance = charge_progress > 0.0f
                                       ? std::fmod(rain_head - i + LED_COUNT, static_cast<float>(LED_COUNT))
                                       : std::fmod(i - rain_head + LED_COUNT, static_cast<float>(LED_COUNT));
            uint32_t pixel = 0;
            if (distance < 1.0f)
            {
                pixel = flex_pct > OVERCHARGE_THRESHOLD ? 0xFFFFFF : 0x88FF88;
            }
            else if (distance < trail_length)
            {
                const float fade = 1.0f - distance / trail_length;
                const auto green = static_cast<uint32_t>(brightness * fade);
                const auto blue = static_cast<uint32_t>(brightness * 0.3f * fade * (1.0f + flex_pct));
                pixel = (green << 16) | blue;
                const int glyph_offset = static_cast<int>(elapsed_seconds * animation_speed);
                const int phase = charge_progress > 0.0f
                                      ? ((i + glyph_offset) % glyph_spacing + glyph_spacing) % glyph_spacing
                                      : ((i - glyph_offset) % glyph_spacing + glyph_spacing) % glyph_spacing;
                if (phase == 0)
                    pixel = (std::min(255u, green * 3 / 2) << 16) | std::min(255u, blue * 3 / 2);
            }

            if (pulse_active)
            {
                const float pulse_distance =
                    std::fmod(i - current_pulse_head + LED_COUNT, static_cast<float>(LED_COUNT));
                if (pulse_distance < 2.0f)
                    pixel = 0xFFFFFF;
            }

            if (charge_progress > 0.0f)
            {
                const auto glow = static_cast<uint32_t>(24.0f + 156.0f * charge_progress);
                const float blend = charge_progress * 0.9f;
                const auto red = static_cast<uint32_t>(((pixel >> 16) & 0xFF) * (1.0f - blend) + glow * blend);
                const auto green = static_cast<uint32_t>(((pixel >> 8) & 0xFF) * (1.0f - blend) + glow * blend);
                const auto blue = static_cast<uint32_t>((pixel & 0xFF) * (1.0f - blend) + glow * blend);
                pixel = (red << 16) | (green << 8) | blue;
                const int spacing = std::max(3, 8 - density);
                const int phase = ((i + static_cast<int>(elapsed_seconds * animation_speed)) % spacing + spacing) % spacing;
                if (phase == 0)
                {
                    const auto glyph = static_cast<uint32_t>(
                        std::min(255.0f, 120.0f + 135.0f * charge_progress));
                    pixel = (glyph << 16) | (glyph << 8) | glyph;
                }
            }
            pixels[i] = pixel;
        }

        std::cout << "\033[2J\033[H"
                  << "Flex LED simulator (Ctrl+C to quit)  |  "
                  << (glitch_active ? "GLITCH" : charge_progress >= 1.0f ? "OVERCHARGE" :
                      charge_progress > 0.0f ? "CHARGING" : pulse_active ? "PULSE" : "RAIN")
                  << "  | flex " << static_cast<int>(flex_pct * 100) << "%"
                  << "  | glyph density " << density << "/5"
                  << "  | charge " << static_cast<int>(charge_progress * 100) << "%\n\n";
        for (int i = 0; i < LED_COUNT; i += 2)
        {
            print_pixel(pixels[i]);
            print_pixel(pixels[i + 1]);
        }
        std::cout << "\n\nDemo cycles through a pulse, two held double-flex glitches, and an overcharge.\n"
                  << std::flush;

        next_frame += FRAME_DURATION;
        std::this_thread::sleep_until(next_frame);
    }
}
