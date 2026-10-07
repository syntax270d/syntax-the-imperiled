#include <iostream>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include "ws2811.h"

#define LED_COUNT 144
#define TARGET_FREQ WS2811_TARGET_FREQ
#define GPIO_PIN 18
#define DMA 10

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
}

int main() {
    std::srand(std::time(nullptr));
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
    int error_ticks = 0;

    while (true) {
        int raw_flex = read_adc(spi_fd);
        if (raw_flex < 0) raw_flex = 0;

        float flex_pct = (raw_flex - 100) / 800.0f;
        if (flex_pct < 0.0f) flex_pct = 0.0f;
        if (flex_pct > 1.0f) flex_pct = 1.0f;

        // 18% Mathematical Corporeal Damage Glitch Logic Loop
        if (error_ticks == 0 && (std::rand() % 100 < 18) && flex_pct < 0.2f) {
            error_ticks = 120; // Locks system into a stutter state for 120 frames (~2 seconds)
        }

        float animation_speed = 0.2f + (flex_pct * 0.8f);
        uint8_t base_brightness = 40 + static_cast<uint8_t>(flex_pct * 215);

        if (error_ticks > 0) {
            animation_speed = 0.02f; // Rain loop drops processing rate to a near stall
            error_ticks--;
        }

        rain_head += animation_speed;
        if (rain_head >= LED_COUNT) rain_head = 0.0f;

        for (int i = 0; i < LED_COUNT; i++) {
            if (error_ticks > 0) {
                // Fragmented, sputtering crimson error code visual array
                if (std::rand() % 10 < 3) ledstring.channel[0].leds[i] = 0x00FF00; // Red in GRB format
                else ledstring.channel[0].leds[i] = 0x000000;
                continue;
            }

            float distance = std::fmod((static_cast<float>(i) - rain_head + LED_COUNT), static_cast<float>(LED_COUNT));
            if (distance < 1.0f) {
                ledstring.channel[0].leds[i] = (flex_pct > 0.8f) ? 0xFFFFFF : 0x88FF88; // White surge edge
            } else if (distance < 15.0f) {
                float fade = 1.0f - (distance / 15.0f);
                uint8_t green = static_cast<uint8_t>(base_brightness * fade);
                uint8_t blue = static_cast<uint8_t>((base_brightness * 0.3f) * fade * (1.0f + flex_pct));
                ledstring.channel[0].leds[i] = (green << 16) | blue;
            } else {
                ledstring.channel[0].leds[i] = 0x000000;
            }
        }

        ws2811_render(&ledstring);
        usleep(16666);
    }

    ws2811_fini(&ledstring);
    close(spi_fd);
    return 0;
}