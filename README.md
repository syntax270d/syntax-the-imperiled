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
`g++ -O2 src/overclock.cpp -o overclock -lws2811`

Execute with root privileges (required for DMA memory access):
`sudo ./overclock`

---

## 📜 Character Lore & Interactive Mechanics

* **The Spatial Clipping Curse:** Processing reality at sub-atomic speeds causes severe motor-frame drops and spatial packet loss[cite: 5]. Navigating doorframes inflicts -1HP elbow clipping damage, while climbing staircases triggers a -2HP collision check error[cite: 5].
* **18% Corporeal Glitch Loop:** The core software (`overclock.cpp`) executes a randomized 18% mathematical stall loop, dropping animation speeds and sputtering crimson red error states across the forearm array[cite: 5, 6].
* **Muscle Overclocking:** Squeezing or flexing the forearm-mounted FSR 402 pressure sensor overrides the error loop, accelerating animation frame rates and triggering a white-hot hardware reboot surge[cite: 5, 6].