# ⚡ Syntax the Imperiled — Embedded Cybernetic Cosplay Engine

An embedded C++ hardware control system and costume codebase built for **Syntax the Imperiled**—an original technomancer character burdened by a spatial clipping curse.

Powered by a **Raspberry Pi 3 Model B**, this repository interfaces SPI/ADC sensory hardware with high-density LED arrays to drive real-time light animations, dynamic muscle-flexing overclocks, and interactive glitch loops.

---

## 🛠️ System Architecture & Stack

* **Compute Unit:** Raspberry Pi 3 Model B (2017 Architecture) running Raspberry Pi OS Legacy (32-bit)
* **Light Engine:** BTF-LIGHTING WS2812B-ECO (144 LEDs/m, 5V) encased in a 10mm hollow flexible silicone neon diffusion sleeve
* **Myoelectric / Sensory Input:** Adafruit FSR 402 Pressure Sensor read via an Adafruit MCP3008 8-channel 10-bit ADC DIP-16 over SPI (`/dev/spidev0.0`)
* **Production Runtime:** C++ (`src/overclock.cpp`) utilizing Jeremy Garff's `rpi_ws281x` library for direct DMA channel 10 access
* **Power Protocol:** Isolated parallel 5V power routing directly from external power banks to protect Raspberry Pi GPIO traces

---

## 📁 Repository Structure

```text
syntax-the-imperiled/
├── README.md             <- Master system summary & quickstart guide
├── LICENSE               <- Permissive open-source license (MIT)
├── .gitignore            <- Excludes compiled binaries, objects, and logs
├── docs/
│   ├── clothes.md        <- Wardrobe manifest, sizing, layers & chest rig layout
│   └── light_assembly.md <- Hardware pinout schematics & C++ source code
└── src/
    └── overclock.cpp     <- Core C++ production driver (WS2811 + FSR loop)
```

---

## 🚀 Quickstart & Hardware Setup

### 1. Raspberry Pi Interface Setup
Enable SPI on your Raspberry Pi:
`sudo raspi-config`
(Navigate to Interface Options -> SPI -> Enable -> Finish)

Verify the SPI device interface:
`ls /dev/spidev0.0`

### 2. Dependencies & Build Instructions
Install required C++ build tools and the rpi_ws281x driver library:
`sudo apt update && sudo apt install -y build-essential cmake git scons`
`git clone https://github.com/jgarff/rpi_ws281x.git`
`cd rpi_ws281x`
`scons`
`sudo scons install`

### 3. Compilation & Execution
Compile the C++ engine:
`g++ -std=c++17 -O2 src/overclock.cpp src/animation.cpp -o overclock -lws2811`

Execute with root privileges (required for DMA memory access):
`sudo ./overclock`

### Shared Animation Tests And ASCII Preview
Build and run the shared-logic tests on a regular Linux or Windows development machine; this does not access SPI or require `rpi_ws281x`:
`g++ -std=c++17 -O2 src/simulator.cpp src/animation.cpp -o simulator`
`./simulator`

By default, `simulator` runs deterministic assertions against the same animation logic used by the hardware driver and exits nonzero if a test fails. Run `./simulator --demo` for the looping ASCII preview of those shared calculations. The preview uses ANSI true-color escape sequences, so run it in a color-capable terminal; press `Ctrl+C` to quit.

---

## 📜 Character Lore & Interactive Mechanics

* **The Spatial Clipping Curse:** Processing reality at sub-atomic speeds causes severe motor-frame drops and spatial packet loss[cite: 5]. Navigating doorframes inflicts -1HP elbow clipping damage, while climbing staircases triggers a -2HP collision check error[cite: 5].
* **Pulse:** Crossing a light-flex threshold sends a short bright ripple down the strip. Pulses last 180 ms and have a 500 ms cooldown.
* **Corporeal Glitch:** Flex twice above the gesture threshold, with the second flex held for 500 ms within 1.5 seconds of the first, to trigger a two-second crimson stutter.
* **Flex Combos:** Repeated flexes within a three-second window increase the rain trail and glyph density, up to five density steps. Holding a medium flex adds a gentle brightness breath.
* **Overcharge:** Hold a tight flex for three seconds to reverse the flow and ramp into a bright white glow with brighter glyphs. Releasing powers the effect down over 600 ms; releasing before the hold completes cancels the charge.

Gesture thresholds use normalized sensor values and may need calibration for the installed FSR. The thresholds and animation timings are centralized in `src/animation.cpp`.