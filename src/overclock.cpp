#include "animation.h"
#include "ws2811.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#define TARGET_FREQ WS2811_TARGET_FREQ
#define GPIO_PIN 18
#define DMA 10

namespace
{
    const char *SPI_DEVICE = "/dev/spidev0.0";
    const uint8_t SPI_MODE = SPI_MODE_0;
    const uint8_t SPI_BITS = 8;
    const uint32_t SPI_SPEED = 1000000;

    ws2811_t ledstring = {};

    int read_adc(int fd)
    {
        uint8_t tx[] = {1, static_cast<uint8_t>((8 + 0) << 4), 0};
        uint8_t rx[3] = {};
        spi_ioc_transfer transfer = {};
        transfer.tx_buf = reinterpret_cast<unsigned long>(tx);
        transfer.rx_buf = reinterpret_cast<unsigned long>(rx);
        transfer.len = 3;
        transfer.speed_hz = SPI_SPEED;
        transfer.bits_per_word = SPI_BITS;

        if (ioctl(fd, SPI_IOC_MESSAGE(1), &transfer) < 1)
            return -1;
        return ((rx[1] & 3) << 8) + rx[2];
    }
}

int main()
{
    const int spi_fd = open(SPI_DEVICE, O_RDWR);
    if (spi_fd < 0 ||
        ioctl(spi_fd, SPI_IOC_WR_MODE, &SPI_MODE) < 0 ||
        ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &SPI_SPEED) < 0)
    {
        std::cerr << "[-] SPI Initialization Failure.\n";
        if (spi_fd >= 0)
            close(spi_fd);
        return 1;
    }

    ledstring.freq = TARGET_FREQ;
    ledstring.dmanum = DMA;
    ledstring.channel[0].gpionum = GPIO_PIN;
    ledstring.channel[0].invert = 0;
    ledstring.channel[0].count = overclock::LED_COUNT;
    ledstring.channel[0].brightness = 255;
    ledstring.channel[0].strip_type = WS2811_STRIP_GRB;

    if (ws2811_init(&ledstring) != WS2811_SUCCESS)
    {
        close(spi_fd);
        std::cerr << "[-] WS2811 Initialization Failure.\n";
        return 1;
    }

    overclock::Animator animator;
    const auto start = std::chrono::steady_clock::now();
    while (true)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
        const int raw_flex = read_adc(spi_fd);
        if (raw_flex < 0)
        {
            std::cerr << "[-] SPI ADC read failure.\n";
            ws2811_fini(&ledstring);
            close(spi_fd);
            return 1;
        }
        const float flex_pct = overclock::normalize_adc(raw_flex);
        const overclock::Frame frame = animator.update(flex_pct, elapsed);

        for (int i = 0; i < overclock::LED_COUNT; ++i)
            ledstring.channel[0].leds[i] = frame.pixels[i];

        if (ws2811_render(&ledstring) != WS2811_SUCCESS)
        {
            std::cerr << "[-] WS2811 Render Failure.\n";
            ws2811_fini(&ledstring);
            close(spi_fd);
            return 1;
        }
        usleep(16666);
    }

    ws2811_fini(&ledstring);
    close(spi_fd);
    return 0;
}
