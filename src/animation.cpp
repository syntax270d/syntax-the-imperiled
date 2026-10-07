#include "animation.h"

#include <algorithm>
#include <cmath>

namespace overclock
{
    namespace
    {
        constexpr std::array<float, STREAM_COUNT> STREAM_SPEEDS = {
            0.76f, 0.91f, 1.0f, 1.13f, 1.29f};
        constexpr std::array<float, STREAM_COUNT> STREAM_TRAIL_SCALES = {
            0.82f, 1.0f, 0.9f, 1.12f, 0.76f};
        constexpr float PULSE_THRESHOLD = 0.20f;
        constexpr float FLEX_THRESHOLD = 0.45f;
        constexpr float FLEX_RELEASE_THRESHOLD = 0.40f;
        constexpr float OVERCHARGE_THRESHOLD = 0.80f;
        constexpr float OVERCHARGE_RELEASE_THRESHOLD = 0.75f;
        constexpr auto PULSE_DURATION = std::chrono::milliseconds(180);
        constexpr auto PULSE_COOLDOWN = std::chrono::milliseconds(500);
        constexpr auto DOUBLE_FLEX_WINDOW = std::chrono::milliseconds(1500);
        constexpr auto DOUBLE_FLEX_HOLD = std::chrono::milliseconds(500);
        constexpr auto GLITCH_DURATION = std::chrono::seconds(2);
        constexpr auto OVERCHARGE_HOLD = std::chrono::seconds(3);
        constexpr auto POWER_DOWN_DURATION = std::chrono::milliseconds(600);
        constexpr auto FLEX_COMBO_WINDOW = std::chrono::seconds(3);

        uint32_t scale_pixel(uint32_t pixel, float scale)
        {
            const auto red = std::min(255u, static_cast<uint32_t>(((pixel >> 16) & 0xFF) * scale));
            const auto green = std::min(255u, static_cast<uint32_t>(((pixel >> 8) & 0xFF) * scale));
            const auto blue = std::min(255u, static_cast<uint32_t>((pixel & 0xFF) * scale));
            return (red << 16) | (green << 8) | blue;
        }

        uint32_t pixel_strength(uint32_t pixel)
        {
            return ((pixel >> 16) & 0xFF) + ((pixel >> 8) & 0xFF) + (pixel & 0xFF);
        }

        int positive_mod(int value, int modulus)
        {
            return (value % modulus + modulus) % modulus;
        }
    }

    float normalize_adc(int raw_adc)
    {
        return std::clamp((raw_adc - 100) / 800.0f, 0.0f, 1.0f);
    }

    Animator::Animator()
    {
        for (int stream = 0; stream < STREAM_COUNT; ++stream)
            rain_heads_[stream] = static_cast<float>(LED_COUNT * stream) / STREAM_COUNT;
    }

    Frame Animator::update(float flex_pct, std::chrono::milliseconds elapsed)
    {
        flex_pct = std::clamp(flex_pct, 0.0f, 1.0f);
        const auto now = elapsed;
        const float frame_scale = has_previous_frame_
                                      ? std::chrono::duration<float>(now - previous_frame_).count() * 60.0f
                                      : 0.0f;
        previous_frame_ = now;
        has_previous_frame_ = true;

        if (flex_pct >= PULSE_THRESHOLD && !pulse_threshold_active_)
        {
            pulse_threshold_active_ = true;
            if (now >= pulse_cooldown_until_)
            {
                pulse_active_ = true;
                pulse_head_ = rain_heads_[0];
                pulse_start_ = now;
                pulse_until_ = now + PULSE_DURATION;
                pulse_cooldown_until_ = now + PULSE_COOLDOWN;
            }
        }
        else if (flex_pct < PULSE_THRESHOLD - 0.05f)
        {
            pulse_threshold_active_ = false;
        }

        if (flex_pct >= FLEX_THRESHOLD && !gesture_active_)
        {
            gesture_active_ = true;
            recent_flexes_.erase(
                std::remove_if(
                    recent_flexes_.begin(), recent_flexes_.end(),
                    [now](const auto &time) { return now - time > FLEX_COMBO_WINDOW; }),
                recent_flexes_.end());
            recent_flexes_.push_back(now);

            if (has_first_flex_ && now - first_flex_time_ <= DOUBLE_FLEX_WINDOW)
            {
                waiting_for_double_flex_hold_ = true;
                double_flex_hold_start_ = now;
                has_first_flex_ = false;
            }
            else
            {
                has_first_flex_ = true;
                first_flex_time_ = now;
            }
        }
        else if (flex_pct < FLEX_RELEASE_THRESHOLD && gesture_active_)
        {
            gesture_active_ = false;
            if (waiting_for_double_flex_hold_)
            {
                waiting_for_double_flex_hold_ = false;
                has_first_flex_ = false;
            }
        }

        if (has_first_flex_ && now - first_flex_time_ > DOUBLE_FLEX_WINDOW)
            has_first_flex_ = false;

        if (waiting_for_double_flex_hold_ && flex_pct >= OVERCHARGE_THRESHOLD)
        {
            waiting_for_double_flex_hold_ = false;
            has_first_flex_ = false;
        }
        else if (waiting_for_double_flex_hold_ &&
                 now - double_flex_hold_start_ >= DOUBLE_FLEX_HOLD)
        {
            glitch_start_ = now;
            glitch_until_ = now + GLITCH_DURATION;
            waiting_for_double_flex_hold_ = false;
            has_first_flex_ = false;
        }

        const bool tightly_flexed = charge_tracking_
                                        ? flex_pct >= OVERCHARGE_RELEASE_THRESHOLD
                                        : flex_pct >= OVERCHARGE_THRESHOLD;
        if (!powering_down_ && tightly_flexed)
        {
            if (!charge_tracking_)
            {
                charge_tracking_ = true;
                charge_start_ = now;
            }
            charge_progress_ = std::min(
                1.0f,
                std::chrono::duration<float>(now - charge_start_).count() /
                    std::chrono::duration<float>(OVERCHARGE_HOLD).count());
        }
        else if (!powering_down_ && charge_tracking_)
        {
            powering_down_ = true;
            power_down_start_ = now;
            power_down_charge_ = charge_progress_;
            charge_tracking_ = false;
        }

        if (powering_down_)
        {
            const auto elapsed_down = now - power_down_start_;
            const float fade = std::max(
                0.0f,
                1.0f - std::chrono::duration<float>(elapsed_down).count() /
                    std::chrono::duration<float>(POWER_DOWN_DURATION).count());
            charge_progress_ = power_down_charge_ * fade;
            if (elapsed_down >= POWER_DOWN_DURATION)
            {
                powering_down_ = false;
                charge_progress_ = 0.0f;
            }
        }
        else if (!charge_tracking_)
        {
            charge_progress_ = 0.0f;
        }

        if (pulse_active_ && now >= pulse_until_)
            pulse_active_ = false;

        recent_flexes_.erase(
            std::remove_if(
                recent_flexes_.begin(), recent_flexes_.end(),
                [now](const auto &time) { return now - time > FLEX_COMBO_WINDOW; }),
            recent_flexes_.end());

        const int density = std::min(5, std::max(0, static_cast<int>(recent_flexes_.size()) - 1));
        const float trail_length = 15.0f + density * 5.0f;
        const int glyph_spacing = std::max(4, 10 - density);
        const float elapsed_seconds = std::chrono::duration<float>(now).count();
        float animation_speed = 0.2f + flex_pct * 0.8f;
        uint8_t base_brightness = 40 + static_cast<uint8_t>(flex_pct * 215);

        if (flex_pct >= FLEX_THRESHOLD && flex_pct < OVERCHARGE_THRESHOLD && charge_progress_ == 0.0f)
        {
            const float breathing = 0.875f + 0.125f * std::sin(elapsed_seconds * 2.0f);
            base_brightness = static_cast<uint8_t>(base_brightness * breathing);
        }
        if (pulse_active_)
            base_brightness = static_cast<uint8_t>(std::min(255.0f, base_brightness * 1.25f));
        if (charge_progress_ > 0.0f)
            animation_speed = 1.0f + charge_progress_ * 11.0f;

        for (int stream = 0; stream < STREAM_COUNT; ++stream)
        {
            const float step = animation_speed * STREAM_SPEEDS[stream] * frame_scale;
            if (charge_progress_ > 0.0f)
            {
                rain_heads_[stream] -= step;
                if (rain_heads_[stream] < 0.0f)
                {
                    rain_heads_[stream] = std::fmod(rain_heads_[stream], static_cast<float>(LED_COUNT));
                    if (rain_heads_[stream] < 0.0f)
                        rain_heads_[stream] += LED_COUNT;
                }
            }
            else
            {
                rain_heads_[stream] += step;
                if (rain_heads_[stream] >= LED_COUNT)
                    rain_heads_[stream] = std::fmod(rain_heads_[stream], static_cast<float>(LED_COUNT));
            }
        }

        const bool glitch_active = now < glitch_until_ && charge_progress_ == 0.0f;
        const float pulse_elapsed = std::chrono::duration<float>(now - pulse_start_).count();
        const float current_pulse_head = std::fmod(
            pulse_head_ + pulse_elapsed * 40.0f, static_cast<float>(LED_COUNT));

        Frame frame;
        frame.flex = flex_pct;
        frame.charge = charge_progress_;
        frame.glyph_density = density;
        frame.reversed = charge_progress_ > 0.0f;
        frame.speed_min_leds_per_second = animation_speed * STREAM_SPEEDS.front() * 60.0f;
        frame.speed_max_leds_per_second = animation_speed * STREAM_SPEEDS.back() * 60.0f;

        if (glitch_active)
        {
            frame.mode = Mode::Glitch;
            const auto glitch_elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - glitch_start_).count();
            for (int i = 0; i < LED_COUNT; ++i)
                frame.pixels[i] = ((i * 17 + glitch_elapsed / 40) % 10 < 3) ? 0xFF0000 : 0;
            return frame;
        }

        if (powering_down_)
            frame.mode = Mode::PoweringDown;
        else if (charge_progress_ >= 1.0f)
            frame.mode = Mode::Overcharge;
        else if (charge_progress_ > 0.0f)
            frame.mode = Mode::Charging;
        else if (pulse_active_)
            frame.mode = Mode::Pulse;
        else
            frame.mode = Mode::Rain;

        for (int i = 0; i < LED_COUNT; ++i)
        {
            uint32_t pixel = 0;
            for (int stream = 0; stream < STREAM_COUNT; ++stream)
            {
                const float distance = charge_progress_ > 0.0f
                                           ? std::fmod(rain_heads_[stream] - i + LED_COUNT, static_cast<float>(LED_COUNT))
                                           : std::fmod(i - rain_heads_[stream] + LED_COUNT, static_cast<float>(LED_COUNT));
                uint32_t stream_pixel = 0;
                if (distance < 1.0f)
                {
                    stream_pixel = flex_pct > OVERCHARGE_THRESHOLD ? 0xFFFFFF : 0x88FF88;
                }
                else
                {
                    const float stream_trail_length = trail_length * STREAM_TRAIL_SCALES[stream];
                    if (distance < stream_trail_length)
                    {
                        const float fade = 1.0f - distance / stream_trail_length;
                        const auto green = static_cast<uint32_t>(base_brightness * fade);
                        const auto blue = static_cast<uint32_t>(
                            (base_brightness * 0.3f) * fade * (1.0f + flex_pct));
                        stream_pixel = (green << 16) | blue;
                        const int glyph_offset = static_cast<int>(
                            elapsed_seconds * animation_speed * STREAM_SPEEDS[stream]) + stream * 3;
                        const int glyph_phase = charge_progress_ > 0.0f
                                                    ? positive_mod(i + glyph_offset, glyph_spacing)
                                                    : positive_mod(i - glyph_offset, glyph_spacing);
                        if (glyph_phase == 0)
                            stream_pixel = scale_pixel(stream_pixel, 1.5f);
                    }
                }
                if (pixel_strength(stream_pixel) > pixel_strength(pixel))
                    pixel = stream_pixel;
            }

            if (pulse_active_)
            {
                const float pulse_distance =
                    std::fmod(i - current_pulse_head + LED_COUNT, static_cast<float>(LED_COUNT));
                if (pulse_distance < 2.0f)
                    pixel = scale_pixel(pixel, 1.5f);
            }

            if (charge_progress_ > 0.0f)
            {
                const uint8_t glow = static_cast<uint8_t>(24.0f + 156.0f * charge_progress_);
                const float blend = charge_progress_ * 0.9f;
                const uint32_t red = static_cast<uint32_t>(((pixel >> 16) & 0xFF) * (1.0f - blend) + glow * blend);
                const uint32_t green = static_cast<uint32_t>(((pixel >> 8) & 0xFF) * (1.0f - blend) + glow * blend);
                const uint32_t blue = static_cast<uint32_t>((pixel & 0xFF) * (1.0f - blend) + glow * blend);
                pixel = (red << 16) | (green << 8) | blue;
                const int spacing = std::max(3, 8 - density);
                const int glyph_offset = static_cast<int>(elapsed_seconds * animation_speed);
                if (positive_mod(i + glyph_offset, spacing) == 0)
                {
                    const uint8_t glyph = static_cast<uint8_t>(
                        std::min(255.0f, 120.0f + 135.0f * charge_progress_));
                    pixel = (glyph << 16) | (glyph << 8) | glyph;
                }
            }
            frame.pixels[i] = pixel;
        }

        return frame;
    }
}
