# Headless FM Synth (ESP32)

A fully headless, Web Bluetooth-controlled FM Synthesizer running on the Wemos Lolin32 Lite (ESP32).

## Features

*   **FM Synthesis Engine**: 2-Operator FM (Phase Modulation) with adjustable Ratio and Index.
*   **Analog Modeling**: Integrated **Moog Ladder Filter** (Microtracker model) for warm, resonant filtering.
*   **Headless Design**: No physical controls. Everything is controlled wirelessly via BLE MIDI.
*   **Web Controller**: Single-file Progressive Web App (PWA) for parameter control.
*   **Low Latency**: Audio processing runs on Core 1, while BLE connectivity is handled on Core 0.

## Hardware Requirements

*   **Board**: Wemos Lolin32 Lite (or compatible ESP32 board).
*   **Audio DAC**: PCM5102 or PT8211 (I2S).
    *   **BCK**: GPIO 26
    *   **WS (LRCK)**: GPIO 25
    *   **DATA**: GPIO 22

## Architecture

*   **Core 0**: Handles BLE MIDI stack and control messages.
*   **Core 1**: dedicated to high-priority Audio Processing (FM Engine + Filter).
*   **Signal Flow**: `FM Voice -> Moog Filter -> I2S Output`.

## Control (Web App)

The synth hosts a Web Bluetooth interface.

1.  Open `index.html` in a Chrome/Edge browser (Desktop or Android).
2.  Click **Connect to Synth**.
3.  Select **32fm - ESP32 Synthesizer** from the device list.
4.  Use the sliders to control parameters in real-time.

### MIDI CC Mapping

| Parameter | MIDI CC | Description |
| :--- | :--- | :--- |
| **Filter Cutoff** | 74 | Exponential mapping (20Hz - 20kHz) |
| **Filter Res** | 71 | Resonance (can self-oscillate) |
| **FM Ratio** | 16 | Carrier/Modulator Ratio |
| **FM Index** | 17 | Modulation Amount |
| **Attack** | 73 | Amplitude Envelope Attack |
| **Decay** | 75 | Amplitude Envelope Decay |
| **Sustain** | 79 | Amplitude Envelope Sustain |
| **Release** | 72 | Amplitude Envelope Release |

## Build & Installation

### Dependencies
*   [ML_SynthTools](https://github.com/marcel-licence/ML_SynthTools) (Library)
*   ESP32 Board Support Package (Arduino IDE)

### Compiling
1.  Install the **ML_SynthTools** library in your Arduino libraries folder.
2.  Open `esp32_fm_synth.ino` in Arduino IDE.
3.  Select Board: **WEMOS LOLIN32 Lite**.
4.  Upload to the board.
5.  Upload `index.html` to a web host or open locally to control the device.

## License

This project utilizes code from:
*   [MoogLadders](https://github.com/ovelhaaa/MoogLadders) (Microtracker Model) - Unlicense/MIT.
*   [ML_SynthTools](https://github.com/marcel-licence/ML_SynthTools) - See original license.
