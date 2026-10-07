#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <cmath>
#include "ws2811.h"

#define LED_COUNT 144
#define TARGET_FREQ WS2811_TARGET_FREQ
#define GPIO_PIN 18
#define DMA 10

using Clock = std::chrono::steady_clock;

namespace {
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

uint32_t scale_pixel(uint32_t pixel, float scale) {
    const uint32_t red = std::min(255u, static_cast<uint32_t>(((pixel >> 16) & 0xFF) * scale));
    const uint32_t green = std::min(255u, static_cast<uint32_t>(((pixel >> 8) & 0xFF) * scale));
    const uint32_t blue = std::min(255u, static_cast<uint32_t>((pixel & 0xFF) * scale));
    return (red << 16) | (green << 8) | blue;
}
}

const char* SPI_DEVICE = "/dev/spidev0.0";
const uint8_t SPI_MODE = SPI_MODE_0;
const uint8_t SPI_BITS = 8;
const uint32_t SPI_SPEED = 1000000;

ws2811_t ledstring = {
    .freq = TARGET_FREQ,
    .dmanum = DMA,
    .channel = {
        [0] = {
            .gpionum = GPIO_PIN,
            .count = LED_COUNT,
            .invert = 0,
            .brightness = 255,
            .type = WS2811_STRIP_GRB,
        },
    },
};

int read_adc(int fd) {
    uint8_t tx[] = { 1, static_cast<uint8_t>((8 + 0) << 4), 0 };
    uint8_t rx[3] = {0};
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = 3,
        .speed_hz = SPI_SPEED,
        .bits_per_word = SPI_BITS,
        .mode = SPI_MODE,
    };
    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 1) return -1;
    return ((rx[1] & 3) << 8) + rx[2];
};

int main() {
    int spi_fd = open(SPI_DEVICE, O_RDWR);
    if (spi_fd < 0 || ioctl(spi_fd, SPI_IOC_WR_MODE, &SPI_MODE) < 0 || ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &SPI_SPEED) < 0) {
        std::cerr << "[-] SPI Initialization Failure." << std::endl;
        return 1;
    }
    if (ws2811_init(&ledstring) != WS2811_SUCCESS) {
        close(spi_fd);
        return 1;
    }

    float rain_head = 0.0f;
    bool pulse_threshold_active = false;
    bool gesture_active = false;
    bool has_first_flex = false;
    bool waiting_for_double_flex_hold = false;
    bool charge_tracking = false;
    bool powering_down = false;
    bool pulse_active = false;
    float pulse_head = 0.0f;
    float charge_progress = 0.0f;
    float power_down_charge = 0.0f;
    Clock::time_point first_flex_time{};
    Clock::time_point double_flex_hold_start{};
    Clock::time_point charge_start{};
    Clock::time_point power_down_start{};
    Clock::time_point pulse_start{};
    Clock::time_point pulse_until{};
    Clock::time_point pulse_cooldown_until{};
    Clock::time_point glitch_start{};
    Clock::time_point glitch_until{};
    std::deque<Clock::time_point> recent_flexes;

    while (true) {
        const Clock::time_point now = Clock::now();
        int raw_flex = read_adc(spi_fd);
        if (raw_flex < 0) raw_flex = 0;

        float flex_pct = (raw_flex - 100) / 800.0f;
        if (flex_pct < 0.0f) flex_pct = 0.0f;
        if (flex_pct > 1.0f) flex_pct = 1.0f;

        if (flex_pct >= PULSE_THRESHOLD && !pulse_threshold_active) {
            pulse_threshold_active = true;
            if (now >= pulse_cooldown_until) {
                pulse_active = true;
                pulse_head = rain_head;
                pulse_start = now;
                pulse_until = now + PULSE_DURATION;
                pulse_cooldown_until = now + PULSE_COOLDOWN;
            }
        } else if (flex_pct < PULSE_THRESHOLD - 0.05f) {
            pulse_threshold_active = false;
        }

        if (flex_pct >= FLEX_THRESHOLD && !gesture_active) {
            gesture_active = true;
            while (!recent_flexes.empty() && now - recent_flexes.front() > FLEX_COMBO_WINDOW) {
                recent_flexes.pop_front();
            }
            recent_flexes.push_back(now);

            if (has_first_flex && now - first_flex_time <= DOUBLE_FLEX_WINDOW) {
                waiting_for_double_flex_hold = true;
                double_flex_hold_start = now;
                has_first_flex = false;
            } else {
                has_first_flex = true;
                first_flex_time = now;
            }
        } else if (flex_pct < FLEX_RELEASE_THRESHOLD && gesture_active) {
            gesture_active = false;
            if (waiting_for_double_flex_hold) {
                waiting_for_double_flex_hold = false;
                has_first_flex = false;
            }
        }

        if (has_first_flex && now - first_flex_time > DOUBLE_FLEX_WINDOW) {
            has_first_flex = false;
        }

        if (waiting_for_double_flex_hold && flex_pct >= OVERCHARGE_THRESHOLD) {
            waiting_for_double_flex_hold = false;
            has_first_flex = false;
        } else if (waiting_for_double_flex_hold &&
                   now - double_flex_hold_start >= DOUBLE_FLEX_HOLD) {
            glitch_start = now;
            glitch_until = now + GLITCH_DURATION;
            waiting_for_double_flex_hold = false;
            has_first_flex = false;
        }

        const bool tightly_flexed = charge_tracking
            ? flex_pct >= OVERCHARGE_RELEASE_THRESHOLD
            : flex_pct >= OVERCHARGE_THRESHOLD;
        if (!powering_down && tightly_flexed) {
            if (!charge_tracking) {
                charge_tracking = true;
                charge_start = now;
            }
            const auto held_for = now - charge_start;
            charge_progress = std::min(
                1.0f,
                std::chrono::duration<float>(held_for).count() /
                    std::chrono::duration<float>(OVERCHARGE_HOLD).count());
        } else if (!powering_down && charge_tracking) {
            powering_down = true;
            power_down_start = now;
            power_down_charge = charge_progress;
            charge_tracking = false;
        }

        if (powering_down) {
            const auto elapsed = now - power_down_start;
            const float fade = std::max(
                0.0f,
                1.0f - std::chrono::duration<float>(elapsed).count() /
                    std::chrono::duration<float>(POWER_DOWN_DURATION).count());
            charge_progress = power_down_charge * fade;
            if (elapsed >= POWER_DOWN_DURATION) {
                powering_down = false;
                charge_progress = 0.0f;
            }
        } else if (!charge_tracking) {
            charge_progress = 0.0f;
        }

        if (pulse_active && now >= pulse_until) {
            pulse_active = false;
        }

        while (!recent_flexes.empty() && now - recent_flexes.front() > FLEX_COMBO_WINDOW) {
            recent_flexes.pop_front();
        }
        const int density = std::min(5, std::max(0, static_cast<int>(recent_flexes.size()) - 1));
        const float trail_length = 15.0f + density * 5.0f;
        const int glyph_spacing = std::max(4, 10 - density);
        const float elapsed_seconds = std::chrono::duration<float>(now.time_since_epoch()).count();
        float animation_speed = 0.2f + (flex_pct * 0.8f);
        uint8_t base_brightness = 40 + static_cast<uint8_t>(flex_pct * 215);

        if (flex_pct >= FLEX_THRESHOLD && flex_pct < OVERCHARGE_THRESHOLD && charge_progress == 0.0f) {
            const float breathing = 0.875f + 0.125f * std::sin(elapsed_seconds * 2.0f);
            base_brightness = static_cast<uint8_t>(base_brightness * breathing);
        }
        if (pulse_active) {
            base_brightness = static_cast<uint8_t>(std::min(255.0f, base_brightness * 1.25f));
        }
        if (charge_progress > 0.0f) {
            animation_speed = 1.0f + charge_progress * 11.0f;
            rain_head -= animation_speed;
            if (rain_head < 0.0f) rain_head += LED_COUNT;
        } else {
            rain_head += animation_speed;
            if (rain_head >= LED_COUNT) rain_head -= LED_COUNT;
        }

        const bool glitch_active = now < glitch_until && charge_progress == 0.0f;
        const float pulse_elapsed = std::chrono::duration<float>(now - pulse_start).count();
        const float current_pulse_head = std::fmod(pulse_head + pulse_elapsed * 40.0f, static_cast<float>(LED_COUNT));

        for (int i = 0; i < LED_COUNT; i++) {
            if (glitch_active) {
                const auto glitch_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - glitch_start).count();
                ledstring.channel[0].leds[i] = ((i * 17 + glitch_elapsed / 40) % 10 < 3) ? 0xFF0000 : 0x000000;
                continue;
            }

            const float distance = charge_progress > 0.0f
                ? std::fmod(rain_head - i + LED_COUNT, static_cast<float>(LED_COUNT))
                : std::fmod(i - rain_head + LED_COUNT, static_cast<float>(LED_COUNT));
            uint32_t pixel = 0x000000;
            if (distance < 1.0f) {
                pixel = (flex_pct > OVERCHARGE_THRESHOLD) ? 0xFFFFFF : 0x88FF88;
            } else if (distance < trail_length) {
                const float fade = 1.0f - (distance / trail_length);
                uint8_t green = static_cast<uint8_t>(base_brightness * fade);
                uint8_t blue = static_cast<uint8_t>((base_brightness * 0.3f) * fade * (1.0f + flex_pct));
                pixel = (green << 16) | blue;
                const int glyph_offset = static_cast<int>(elapsed_seconds * animation_speed);
                const int glyph_phase = charge_progress > 0.0f
                    ? ((i + glyph_offset) % glyph_spacing + glyph_spacing) % glyph_spacing
                    : ((i - glyph_offset) % glyph_spacing + glyph_spacing) % glyph_spacing;
                if (glyph_phase == 0) {
                    pixel = scale_pixel(pixel, 1.5f);
                }
            }

            if (pulse_active) {
                const float pulse_distance = std::fmod(i - current_pulse_head + LED_COUNT, static_cast<float>(LED_COUNT));
                if (pulse_distance < 2.0f) {
                    pixel = scale_pixel(pixel, 1.5f);
                }
            }

            if (charge_progress > 0.0f) {
                const uint8_t glow = static_cast<uint8_t>(24.0f + 156.0f * charge_progress);
                const float blend = charge_progress * 0.9f;
                const uint32_t red = static_cast<uint32_t>(((pixel >> 16) & 0xFF) * (1.0f - blend) + glow * blend);
                const uint32_t green = static_cast<uint32_t>(((pixel >> 8) & 0xFF) * (1.0f - blend) + glow * blend);
                const uint32_t blue = static_cast<uint32_t>((pixel & 0xFF) * (1.0f - blend) + glow * blend);
                pixel = (red << 16) | (green << 8) | blue;
                const int glyph_offset = static_cast<int>(elapsed_seconds * animation_speed);
                const int overcharge_spacing = std::max(3, 8 - density);
                const int glyph_phase = ((i + glyph_offset) % overcharge_spacing + overcharge_spacing) % overcharge_spacing;
                if (glyph_phase == 0) {
                    const uint8_t glyph = static_cast<uint8_t>(std::min(255.0f, 120.0f + 135.0f * charge_progress));
                    pixel = (glyph << 16) | (glyph << 8) | glyph;
                }
            }

            ledstring.channel[0].leds[i] = pixel;
        }

        ws2811_render(&ledstring);
        usleep(16666);
    }

    ws2811_fini(&ledstring);
    close(spi_fd);
    return 0;
}