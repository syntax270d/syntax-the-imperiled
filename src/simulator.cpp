#include "animation.h"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#else
#include <csignal>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace
{
    using Milliseconds = std::chrono::milliseconds;
    constexpr auto SPACE_REPEAT_TIMEOUT = Milliseconds(700);

#ifndef _WIN32
    volatile std::sig_atomic_t stop_requested = 0;

    void handle_interrupt(int)
    {
        stop_requested = 1;
    }
#endif

    class KeyboardInput
    {
    public:
        KeyboardInput()
        {
#ifndef _WIN32
            if (!isatty(STDIN_FILENO))
                return;

            if (tcgetattr(STDIN_FILENO, &original_terminal_) != 0)
                throw std::runtime_error(
                    std::string("Could not read terminal settings: ") + std::strerror(errno));

            auto terminal = original_terminal_;
            terminal.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
            terminal.c_cc[VMIN] = 0;
            terminal.c_cc[VTIME] = 0;
            if (tcsetattr(STDIN_FILENO, TCSANOW, &terminal) != 0)
                throw std::runtime_error(
                    std::string("Could not enable keyboard input: ") + std::strerror(errno));
            terminal_configured_ = true;

            previous_interrupt_handler_ = std::signal(SIGINT, handle_interrupt);
            if (previous_interrupt_handler_ == SIG_ERR)
            {
                tcsetattr(STDIN_FILENO, TCSANOW, &original_terminal_);
                terminal_configured_ = false;
                throw std::runtime_error("Could not install interrupt handler");
            }
            interrupt_handler_installed_ = true;
#endif
        }

        ~KeyboardInput()
        {
#ifndef _WIN32
            if (terminal_configured_ && tcsetattr(STDIN_FILENO, TCSANOW, &original_terminal_) != 0)
                std::cerr << "Could not restore terminal settings: " << std::strerror(errno) << '\n';
            if (interrupt_handler_installed_)
                std::signal(SIGINT, previous_interrupt_handler_);
#endif
        }

        bool available() const
        {
#ifdef _WIN32
            return true;
#else
            return terminal_configured_;
#endif
        }

        bool poll_space_held()
        {
#ifdef _WIN32
            while (_kbhit())
            {
                if (_getch() == ' ')
                    record_space_press();
            }
#else
            if (!terminal_configured_)
                return false;

            while (true)
            {
                fd_set input;
                FD_ZERO(&input);
                FD_SET(STDIN_FILENO, &input);
                timeval timeout{};
                const int ready = select(STDIN_FILENO + 1, &input, nullptr, nullptr, &timeout);
                if (ready == 0 || (ready < 0 && errno == EINTR))
                    break;
                if (ready < 0)
                    throw std::runtime_error(
                        std::string("Could not read keyboard input: ") + std::strerror(errno));

                char key = 0;
                if (read(STDIN_FILENO, &key, 1) != 1)
                    break;
                if (key == ' ')
                    record_space_press();
            }
#endif
            return has_space_press_ &&
                   std::chrono::steady_clock::now() - last_space_press_ <= SPACE_REPEAT_TIMEOUT;
        }

    private:
        void record_space_press()
        {
            last_space_press_ = std::chrono::steady_clock::now();
            has_space_press_ = true;
        }

        std::chrono::steady_clock::time_point last_space_press_{};
        bool has_space_press_ = false;

#ifndef _WIN32
        termios original_terminal_{};
        void (*previous_interrupt_handler_)(int) = SIG_DFL;
        bool terminal_configured_ = false;
        bool interrupt_handler_installed_ = false;
#endif
    };

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

        KeyboardInput keyboard;
        bool manual_input = false;
        overclock::Animator animator;
        const auto start = std::chrono::steady_clock::now();
        auto next_frame = start;
#ifdef _WIN32
        while (true)
#else
        while (!stop_requested)
#endif
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

            const bool space_held = keyboard.poll_space_held();
            if (space_held)
                manual_input = true;
            const float flex = manual_input
                                   ? (space_held ? 0.90f : 0.0f)
                                   : current->flex;
            const overclock::Frame frame = animator.update(flex, elapsed);
            std::cout << "\033[2J\033[H"
                      << "Shared LED strip preview (hold Space for pressure; Ctrl+C to quit)\n"
                      << (manual_input
                              ? (space_held ? "Keyboard input: pressure held"
                                            : "Keyboard input: pressure released")
                              : "Demo: ")
                      << (manual_input ? "" : current->label)
                      << (keyboard.available()
                              ? " | hold Space: 90% flex; release: 0%"
                              : " | keyboard unavailable (stdin is not a terminal)")
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

#ifndef _WIN32
            if (stop_requested)
                break;
#endif
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
