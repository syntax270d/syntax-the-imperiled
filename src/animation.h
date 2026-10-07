#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

namespace overclock
{
    constexpr int LED_COUNT = 144;
    constexpr int STREAM_COUNT = 5;

    enum class Mode
    {
        Rain,
        Pulse,
        Glitch,
        Charging,
        Overcharge,
        PoweringDown
    };

    struct Frame
    {
        std::array<uint32_t, LED_COUNT> pixels{};
        Mode mode = Mode::Rain;
        float flex = 0.0f;
        float charge = 0.0f;
        float speed_min_leds_per_second = 0.0f;
        float speed_max_leds_per_second = 0.0f;
        int glyph_density = 0;
        bool reversed = false;
    };

    float normalize_adc(int raw_adc);

    class Animator
    {
    public:
        Animator();
        Frame update(float flex_pct, std::chrono::milliseconds elapsed);

    private:
        std::array<float, STREAM_COUNT> rain_heads_{};
        std::vector<std::chrono::milliseconds> recent_flexes_;
        std::chrono::milliseconds previous_frame_{0};
        std::chrono::milliseconds first_flex_time_{0};
        std::chrono::milliseconds double_flex_hold_start_{0};
        std::chrono::milliseconds charge_start_{0};
        std::chrono::milliseconds power_down_start_{0};
        std::chrono::milliseconds pulse_start_{0};
        std::chrono::milliseconds pulse_until_{0};
        std::chrono::milliseconds pulse_cooldown_until_{0};
        std::chrono::milliseconds glitch_start_{0};
        std::chrono::milliseconds glitch_until_{0};
        float pulse_head_ = 0.0f;
        float charge_progress_ = 0.0f;
        float power_down_charge_ = 0.0f;
        bool has_previous_frame_ = false;
        bool pulse_threshold_active_ = false;
        bool gesture_active_ = false;
        bool has_first_flex_ = false;
        bool waiting_for_double_flex_hold_ = false;
        bool charge_tracking_ = false;
        bool powering_down_ = false;
        bool pulse_active_ = false;
    };
}
