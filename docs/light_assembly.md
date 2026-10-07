# ⚡ Cybernetic Light Assembly & Engineering Documentation

**Hardware Core:** Raspberry Pi 3 Model B (2017 Architecture)

---

## 🗺️ System Interface Schematic

```text
[ Raspberry Pi GPIO Header ]
3.3V (Pin 1) --------------> VDD / VREF (MCP3008 Pins 16 & 15)
GND (Pin 6) --------------> AGND / DGND (MCP3008 Pins 14 & 9)[cite: 5, 6]
MOSI (Pin 19) --------------> DIN (MCP3008 Pin 11)[cite: 5, 6]
MISO (Pin 21) --------------> DOUT (MCP3008 Pin 12)[cite: 5, 6]
SCLK (Pin 23) --------------> CLK (MCP3008 Pin 13)[cite: 5, 6]
CE0 (Pin 24) --------------> CS/SHDN (MCP3008 Pin 10)[cite: 5, 6]

GPIO 18 (Pin 12) -----------> LED Strip DATA Pin[cite: 5, 6]

[ Pressure Sensor Circuit ]
3.3V Line ----> [ FSR 402 Sensor ] ----> MCP3008 CH0 (Pin 1)[cite: 5, 6]
                         |
                         +---> [ 10k Ohm Resistor ] ---> GND[cite: 5, 6]
```

---

## 🔌 The Isolated Power Splicing Protocol

To avoid burning out the delicate copper traces on your 2017 Pi board, do not route the 5V current for the high-density LEDs through the Pi's GPIO pins[cite: 4]. Splice the 5V USB output straight out of the power bank into parallel lines feeding both the micro-USB input of the Pi and the injection leads of the 144 LEDs/m strips independently, sharing a common ground line to stabilize data transfers[cite: 4].

---

## 🛡️ Mechanical Strain Relief (FSR 402 Hardening)

1. Cut a rigid rectangle out of an old credit card or thin plastic (0.5" x 1.5")[cite: 4].
2. Connect flexible female-to-male jumper extension leads to the fragile crimped pins at the sensor's neck[cite: 4].
3. Tape the plastic card flat behind the junction layer, and encapsulate the pins, card, and wires in a thick bead of hot glue or rigid protective epoxy to isolate the silver traces from arm movement[cite: 4].

---

## 💻 Production Software Interface

The hardware loop is driven by [`src/overclock.cpp`](../src/overclock.cpp)[cite: 3].

### Key Features of the Driver:
* **SPI ADC Polling:** Continuously samples channel 0 of the MCP3008 ADC to measure FSR pressure dynamics[cite: 2, 3].
* **DMA Output:** Uses `rpi_ws281x` on DMA Channel 10 / GPIO 18 to render high-frequency LED animations without CPU jitter[cite: 2, 3].
* **Pulse:** Crossing a light-flex threshold sends a short bright ripple, with a 180 ms duration and 500 ms cooldown.
* **Double-flex glitch:** Two flexes above the gesture threshold, with the second held for 500 ms within 1.5 seconds, trigger a two-second crimson stutter.
* **Flex combos:** Repeated flexes within three seconds increase trail and glyph density, while a medium hold adds a gentle brightness breath.
* **Overcharge:** Holding a tight flex for three seconds reverses the flow and ramps into a bright white glow with brighter glyphs. Releasing fades the effect out over 600 ms.

The hardware driver in `src/overclock.cpp` and the ASCII simulator both use the shared animation engine in `src/animation.cpp` / `src/animation.h`. Gesture thresholds and timings live in `src/animation.cpp`; the normalized flex reading is based on the current ADC calibration, so tune those thresholds for the installed sensor. Run `./simulator` to test the shared behavior without hardware, or `./simulator --demo` to preview it in ASCII.