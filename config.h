/*
 * Copyright (c) 2022 Marcel Licence
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CONFIG_H_
#define CONFIG_H_

#ifdef __CDT_PARSER__
#include <cdt.h>
#endif

#define SERIAL_BAUDRATE 115200

#ifdef ESP32
/*
 * Configuration for Wemos Lolin32 Lite (Headless FM Synth)
 */
#define BOARD_ESP32_DOIT /* Using this as base */

/* I2S Pinout for Lolin32 Lite (PCM5102 / PT8211) */
#define I2S_BCK 26
#define I2S_WS 25
#define I2S_DOUT 22

/* this changes latency but also speed of processing */
#define SAMPLE_BUFFER_SIZE 48

/* this will force using const velocity for all notes, remove this to get dynamic velocity */
#define MIDI_USE_CONST_VELOCITY

/* this variable defines the max length of the delay and also the memory consumption */
#define MAX_DELAY   (SAMPLE_RATE/2) /* 1/2s -> @ 44100 samples */

/* Disable physical controls */
//#define PRESSURE_SENSOR_ENABLED
//#define ADC_TO_MIDI_ENABLED

/* Disable other unused features */
//#define ARP_MODULE_ENABLED
//#define MIDI_STREAM_PLAYER_ENABLED
//#define MIDI_VIA_USB_ENABLED
//#define OLED_OSC_DISP_ENABLED

#define MIDI_RECV_FROM_SERIAL

/*
 * include the board configuration
 * there you will find the most hardware depending pin settings
 */
#include <ml_boards.h> /* requires library ML_SynthTools from https://github.com/marcel-licence/ML_SynthTools */

#ifdef BOARD_ESP32_DOIT
// Override pin definitions if they were set in ml_boards.h
#undef I2S_BCK_PIN
#undef I2S_WS_PIN
#undef I2S_DATA_PIN
#define I2S_BCK_PIN I2S_BCK
#define I2S_WS_PIN I2S_WS
#define I2S_DATA_PIN I2S_DOUT

#define MIDI_PORT2_ACTIVE
#define MIDI_RX2_PIN 16 /* U2RRXD */
#define MIDI_TX2_PIN 17

#endif

#define SAMPLE_RATE 48000
#define SAMPLE_SIZE_16BIT

/* Enable BLE MIDI */
#define BLE_MIDI
#define BLE_MIDI_HOST "32fm - ESP32 Synthesizer"

#endif /* ESP32 */

#endif /* CONFIG_H_ */
