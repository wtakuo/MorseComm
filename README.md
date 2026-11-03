# MorseComm - CW keying transmission over ESP-NOW / LoRa

Japanese version: [README.ja.md](README.ja.md)

MorseComm transmits Morse code keying in both directions between Arduino Nesso N1 (ESP32-C6), M5StickS3 / Cardputer (ESP32-S3), and M5StickC Plus2 / M5StickC Plus (ESP32) boards, using ESP-NOW (no Wi-Fi router needed).
Between Nesso N1 boards and Cardputer ADVs with the Cap LoRa-1262, **LoRa (920 MHz band)** can be used instead of ESP-NOW ([LoRa](#lora-nesso-n1--cardputer-adv--cap-lora-1262)). Both are peer-to-peer, with no distinction between master and slave units.
With the radio set to **OFF**, no radio waves are emitted, and a single unit can be used for Morse sending practice (the sidetone and decoding of the sent text still work).
There is also a **terminal mode** in which you type characters in a terminal on a PC to send them, and received characters are shown in the terminal ([Terminal mode](#terminal-mode-term)).

- Like a radio, there are "channels" (1 to 8), and stations tuned to the same channel communicate with each other. With three or more units, everyone on the same channel hears everyone's code.

- A receive tone is played following the other station's keying. A sidetone is played for your own keying.
- Both received code and the code you send are decoded and shown on the screen.
- Both straight keys and iambic paddles (Mode A / B) are supported.
- The top design priority is that the key never gets stuck down when packets are lost ([How it works](#how-it-works)).

## Hardware

The following five models are supported. Different models can communicate with each other.

| Item | Arduino Nesso N1 | M5StickS3 | M5StickC Plus2 | M5StickC Plus |
|---|---|---|---|---|
| MCU | ESP32-C6 | ESP32-S3 | ESP32 (PICO-V3-02) | ESP32 (PICO-D4) |
| Straight key | GPIO4 (Grove pin 4) | GPIO10 (Grove pin 4) | G33 (Grove white wire) | G33 (Grove white wire) |
| Paddle | DIT = GPIO4, DAH = GPIO5 | DIT = GPIO10, DAH = GPIO9 | DIT = G33, DAH = G32 (yellow wire) | DIT = G33, DAH = G32 (yellow wire) |
| Sound | Built-in buzzer (GPIO11, PWM) | Built-in speaker (ES8311, I2S) | Built-in buzzer (G2, PWM) | Built-in buzzer (G2, PWM) |
| Screen | 1.14" 240x135, touch | 1.14" 240x135, no touch | 1.14" 240x135, no touch | 1.14" 240x135, no touch |
| Buttons | KEY1 (front), KEY2 (side) | BtnA (front), BtnB (side) | BtnA (front), BtnB (side) | BtnA (front), BtnB (side) |
| Radio | ESP-NOW / LoRa (SX1262) / OFF | ESP-NOW / OFF | ESP-NOW / OFF | ESP-NOW / OFF |
| Power management | AW32001 + BQ27220 | M5PM1 | None (power held by G4) | AXP192 |
| USB serial | USB CDC | USB CDC | USB-UART (CH9102) | USB-UART (FTDI) |

The M5Stack Cardputer (original) and Cardputer ADV are as follows. They run **the same build**, which detects which one it is at startup
(Cardputer v1.1 has not been tested).

| Item | Cardputer (original) | Cardputer ADV |
|---|---|---|
| MCU | ESP32-S3 (StampS3) | ESP32-S3 (StampS3A) |
| Straight key / paddle | Grove G1 (white wire) = straight / DIT, G2 (yellow wire) = DAH | Same |
| Sound | Built-in speaker (NS4168, I2S) | Built-in speaker + **headphone jack** (ES8311 + NS4150B) |
| Keyboard | 56 keys (GPIO matrix) | 56 keys (TCA8418, I2C) |
| Screen | 1.14" 240x135, no touch | Same |
| Buttons | One button, G0 (BtnA), + keyboard | Same |
| Radio | ESP-NOW / OFF | ESP-NOW / LoRa (with the Cap LoRa-1262) / OFF |
| Power management | None (instead of USB power, it detects a USB serial connection to a PC) | Same |

**Cardputer ADV headphones:** When headphones are plugged in, the hardware mutes the speaker (the software cannot
detect that they are plugged in). Switch **OUTPUT** (SPEAKER / PHONES) on the settings screen to match. PHONES uses smaller
volume steps suitable for headphones, and VOLUME is remembered separately for each output (defaults: speaker 3, headphones 2). OUTPUT is shown only on the ADV.

Connect the key to the Grove port on the unit, with the contacts shorting to GND (internal pull-up, active low). The paddle's DIT and DAH can be swapped on the settings screen.
Pins are collected in `board.h`, and parameters in `config.h`.

## Building and uploading

All you need is `arduino-cli` (1.x). The board packages and libraries for each model, their versions, and the board options are
written as profiles in `sketch.yaml`, and arduino-cli downloads them automatically on the first build.

| Profile | Model | Example port |
|---|---|---|
| `nesson1` | Arduino Nesso N1 | /dev/cu.usbmodemXXXX |
| `sticks3` | M5StickS3 | /dev/cu.usbmodemXXXX |
| `stickcplus2` | M5StickC Plus2 | /dev/cu.usbserial-XXXX |
| `stickcplus` | M5StickC Plus (uploads at 115200 bps) | /dev/cu.usbserial-XXXX |
| `cardputer` | Cardputer (original) / Cardputer ADV (same build, 8MB partition scheme) | /dev/cu.usbmodemXXXX |

Because multiple models are built from the same folder, specify **a separate build directory for each model** (`--build-path`)
(if they share one, uploading fails because it tries to write files built for another model).

To show the git commit hash on the startup screen, run `tools/update-git-version.sh` before building
(it creates `git_version.h`; if it has not been run, or outside git, `unknown` is shown).
If you run `git config core.hooksPath tools/githooks` once, git hooks (`tools/githooks/`) run it automatically on every
commit, checkout, merge, and rebase, so you no longer need to run it by hand (once per clone).

```sh
# Write the commit hash to git_version.h
tools/update-git-version.sh
# Build and upload (example: Cardputer)
arduino-cli compile --profile cardputer --build-path build/cardputer -u -p /dev/cu.usbmodemXXXX .
# Build only
arduino-cli compile --profile nesson1 --build-path build/nesson1 .
```

The key type (mode) and other settings are saved on each unit.

## Usage

### Startup screen

When powered on or reset, a splash screen is shown for 3 seconds. `MorseComm` appears in the center, and below it the build date and time
(year, month, day, hour, minute, second, such as `20261101123734`) and the git commit hash (such as `0820299`; a build that includes
uncommitted changes has a trailing `+`), so you can check which build is on which unit.
The build date and time is when the build was made for each model, so it differs between models even for the same source. Use the commit hash to tell whether the source is the same.
The same information also appears in the serial startup message (`build=... commit=...`) and in `show`.

### Main screen

```
[CH3] x2 -45dBm L0% R0 W0       87%   <- channel / station count / link status / battery (+ = charging)
RX 20 wpm                        [#]   <- receive (other station's estimated speed, key state)
CQ CQ DE JA1ABC K                       (green: received text, 3 lines)
TX 18 wpm                        [#]   <- send (your estimated speed)
TU 73                                   (yellow: sent text, 2 lines)
```

Meaning of the top line:
- `CH3`: the current channel (when using LoRa, `LoRa3` on a purple background; when the radio is OFF, a gray `OFF` and `RADIO OFF`)
- `x2`: the number of stations being received on the same channel (shown only when there are 2 or more)
- `dBm`: the received signal strength of the station heard most recently
- `L`: estimated packet loss rate
- `R`: number of resyncs
- `W`: number of times the watchdog has fired

When no stations are being received any more, `LOST` is shown; if nothing has ever been received on that channel, `NO SIGNAL` is shown.

Code that could not be decoded is shown as a **red `?`** (a correctly received `?` is shown in the normal color).
The prosign `...-.-`, sent continuously without a character gap, is shown as `[SK]`. Prosigns can be added to `prosignTable` in `morse_table.h`.

### Buttons

KEY1 is the front button (BtnA on the StickS3), and KEY2 is the side button (BtnB on the StickS3).

| Screen | Action | Result |
|---|---|---|
| Main | KEY1 (front) short press | Advance the channel by 1 (after 8 comes 1). The new number is shown large in the center of the screen |
| Main | KEY1 long press | Send the stored message once (default `VVV VVV`; set it with `msg`). It is queued after any text still waiting to be sent |
| Main | KEY2 (side) short press | Open the settings screen |
| Main | KEY2 long press | Clear the displayed text |
| Settings (Nesso) | KEY1 short press / KEY2 short press | Next item / previous item |
| Settings (Nesso) | Touch an item | Select that item |
| Settings (Nesso) | Touch the **up** / **down** triangle on the right | Increase / decrease the value (holding it changes the value continuously) |
| Settings (Nesso) | Long press KEY1 or KEY2 | Leave the settings screen (saves) |

The M5StickS3 has no touch panel, so its settings screen is operated in two modes, "select" and "change". The top line shows the current role of each button.

| Mode | BtnA (front) | BtnB (side) | Long press |
|---|---|---|---|
| Select | Next item | Start changing that item (the item turns orange) | Leave the settings screen (saves) |
| Change | Increase the value (+) | Decrease the value (-) | Return to select mode |

On both models, the settings screen automatically returns to the main screen after 30 seconds without any operation.

The settings items are as follows (in on-screen order). Changing a frequency or the volume plays that sound briefly.

| Item | Value |
|---|---|
| RADIO | ESP-NOW / LoRa (Nesso N1, Cardputer ADV + Cap LoRa-1262) / OFF (no radio emission). A value different from the one in use is marked with `*`, and the unit restarts and switches when you leave the settings screen |
| MODE | STRAIGHT / IAMBIC_A / IAMBIC_B |
| PADDLE | NORMAL / SWAPPED (swap DIT and DAH) |
| SPEED | 5 to 50 WPM (keyer speed) |
| SIDETONE | ON / OFF |
| OUTPUT | SPEAKER / PHONES (Cardputer ADV only. Set to PHONES when headphones are plugged in) |
| VOLUME | 0 to 5 (0 is mute. On the Cardputer ADV, separate for each OUTPUT) |
| SIDE FREQ | Sidetone frequency (300 to 1500 Hz, in 50 Hz steps) |
| RX FREQ | Receive tone frequency |
| DEFAULTS | Restore all settings to their defaults. Pressing the up or down triangle (+ or - in change mode on the StickS3 / Plus2) once shows `PRESS AGAIN`, and pressing again within 3 seconds executes it (`RESTORED`). Holding it down does not execute it |

Defaults: RADIO = ESP-NOW, channel = 1, MODE = IAMBIC_B, PADDLE = NORMAL, SPEED = 20 WPM, SIDETONE = ON, VOLUME = 3, SIDE FREQ = 700 Hz, RX FREQ = 600 Hz (the jitter delay is also reset to its default).
If the radio type changes, the unit restarts and switches when you leave the settings screen.

There are 9 items; those that do not fit on the screen are reached by scrolling.

When RADIO is OFF, neither Wi-Fi nor LoRa is initialized, so no radio waves are emitted at all (and nothing is received).
Keying, sidetone, decoding display of sent text, and auto sleep work as usual. Channels are meaningless, so KEY1 (channel switching) has no effect.

### Cardputer keyboard

On the Cardputer, characters typed on the keyboard are sent directly (at the SPEED setting, one character at a time in the order typed).

| Screen | Key | Result |
|---|---|---|
| Main | Letters, digits, symbols | Send that character. Characters not yet sent are shown in gray on the TX line |
| Main | del (BS) | Cancel the last character not yet sent |
| Main | space / enter / tab | Word gap |
| Main | Enclose in `[` `]` | Send continuously without character gaps (e.g. `[SK]`) |
| Main | fn + left (`,`) / fn + right (`/`) | Go back / advance one channel |
| Main | fn + down (`.`) | Send the stored message once (same as KEY1 long press on the other boards) |
| Main | esc (fn + `` ` ``, or just `` ` ``) | Open the settings screen |
| Main | G0 button short press / long press | Open the settings screen / clear the displayed text |
| Settings | up / down (fn + `;` / `.`; fn is optional on the settings screen) | Previous / next item |
| Settings | left / right (`,` / `/`) | Decrease / increase the value |
| Settings | enter (OK) | Advance the value (DEFAULTS executes when pressed twice) |
| Settings | esc, or the G0 button | Leave the settings screen (saves) |

While characters are being sent, key and paddle operation is ignored.

### Auto sleep (on battery)

When running on battery (no USB power), the unit enters deep sleep and the screen turns off if none of the following three occur for 3 minutes.

- Keying
- Button or touch operation
- Receiving code from another station

From 10 seconds before sleeping, a `SLEEP in Ns` warning is shown on the top line. Operating a key or button during this time cancels the sleep.

**Press the front or side button to wake up.** Waking up is a restart and takes about 1 second. Settings remain saved.
Nothing can be received during sleep (LoRa is also stopped). The unit does not sleep while on USB power.
However, **the M5StickC Plus2 cannot detect USB power, so it sleeps after 3 minutes even when connected to USB** (wake with BtnA / BtnB).
The Plus2 keeps the power hold pin (G4) HIGH during sleep, so the power does not turn off.
**The Cardputer cannot detect USB power, so it treats itself as "on USB power" only while it is communicating with a PC over USB serial**
(when connected only to a charger, it sleeps after 3 minutes). Wake it with the G0 button (the keyboard does not wake it). The time can be changed with `SLEEP_TIMEOUT_MS` in `config.h`.

### Serial commands (115200 bps)

Serial output is in English (US-ASCII) only, with CR LF line endings.

- A `morse> ` prompt is shown, and typed characters are echoed back. Backspace deletes one character, and Ctrl-C discards the line being typed.
- When logs or statistics interrupt input, they are printed on a new line, and once the output stops, the prompt and the partially typed characters are printed again.
- `help` lists one command per line.

| Command | Description |
|---|---|
| `ch N` | Communication channel (1 to 8) |
| `radio espnow\|lora\|off` | Radio type (LoRa: Nesso N1, Cardputer ADV + Cap LoRa-1262; off emits no radio waves). Saves and restarts |
| `mode straight\|iambic_a\|iambic_b` | Key mode |
| `wpm N` | Keyer speed |
| `side N` / `side on` / `side off` | Sidetone frequency / on and off |
| `rxtone N` | Receive tone frequency |
| `vol 0-5` | Volume (on the Cardputer ADV, the volume of the current output) |
| `output speaker\|phones` | Sound output (Cardputer ADV only) |
| `swap 0\|1` | Swap the paddle |
| `jitter N` | Jitter buffer delay (ms; default ESP-NOW 150 / LoRa 250. Saved separately for each radio type) |
| `term` | Terminal mode (see below). Exit with Ctrl-C |
| `msg [TEXT]` | Show / set the message sent by KEY1 long press (fn + down on the Cardputer). Up to 64 Morse characters (`[SK]` style prosigns allowed); saved. Default `VVV VVV` |
| `test on [TEXT]` / `test off` | Test mode: keeps automatically keying TEXT at the current WPM. Enclosing in square brackets like `[SK]` sends without character gaps |
| `loss N` | Loss injection: skip sending with probability N% |
| `dropup 0\|1` | Loss injection: skip all packets whose key state is UP |
| `log 0\|1` | Output of the transition log (`TX`/`RX` lines) and decoded characters, plus statistics every 5 seconds |
| `stats` / `show` | Statistics / current settings |
| `clear` / `save` / `help` | Clear the screen / save settings immediately / help |
| `defaults` | Restore all settings to their defaults (runs immediately without confirmation; restarts if the radio type changes) |
| `sleep` | Sleep immediately (for testing) |

In the statistics (`STATS` line), the values after `|` are for the sending side. `csBusy` is the number of times LoRa carrier sense detected another station's signal and waited, and `air` is the total LoRa transmission time.

### Terminal mode (`term`)

Typing `term` in a terminal on a PC connected via USB serial enters terminal mode.

- Typed characters are converted to Morse code and sent one character at a time at the configured speed (SPEED, WPM) (the sidetone also sounds). Typed characters are shown immediately.
- Received characters are shown in the order they arrive. Lines of typed characters start with `> `, and lines of received characters start with `< `.
- Lowercase letters are sent as uppercase. Spaces and newlines become word gaps. Enclosing in square brackets like `[SK]` sends continuously without character gaps (prosigns).
- Backspace cancels characters that have not started being sent yet. Characters with no Morse code ring the bell and are ignored.
- While characters are being sent, key and paddle operation is ignored. Once sending is finished, the key and paddle can be used normally.
- Exit with **Ctrl-C** (or Ctrl-D). Unsent characters are discarded.
- When RADIO is OFF, no radio waves are emitted, so it can be used to practice copying the characters you typed.

Example: on macOS, `screen /dev/cu.usbmodemXXXX 115200` (exit screen with Ctrl-A, K). The Arduino IDE serial monitor also works (text is sent one line at a time).

The log entry `RX D t=... prevU=60` means "went DOWN at time t; the preceding UP lasted 60 ms".
A forced UP by the watchdog is marked `WATCHDOG`, and a subsequent resumption of DOWN is marked `REASSERT`.

## How it works

### Packets

With a scheme that sends each "pressed" or "released" event only once, losing the release packet leaves the receiver stuck in the key-down state.
Instead, this scheme **keeps sending the current state and the recent transition history periodically**.

- **Send interval:** Packets are sent every 20 ms while the key is DOWN and for 500 ms after it goes UP. Otherwise, while idle, a heartbeat is sent every 500 ms. A packet is also sent immediately at the moment the key state changes.
- **Packet contents (13 + 2n bytes):** magic, version, channel, current state, packet sequence number, latest transition ID, send time, and the elapsed times of the last 8 transitions.
- **Receiver processing:** Only transitions whose transition IDs have not been applied yet are applied, oldest first. This handles duplicates and reordering correctly. If more transitions are missed than the 8-entry history covers, the receiver resyncs to the current state.

### Channels and multiple stations

- **Channels:** ESP-NOW stays in broadcast mode, and the channel number is put in the packet. The receiver accepts only packets on the same channel as its own. The physical Wi-Fi channel is fixed (1) for all stations.
- **Multiple stations:** The receiver keeps separate playback processing (described below) and a separate decoder for each sender MAC address (up to 6 stations). Sound is the OR of all stations, and decoded characters from any station appear in the received text.
- **Switching channels:** When the channel is switched, playback of all previous stations is stopped.

### LoRa (Nesso N1 / Cardputer ADV + Cap LoRa-1262)

The Nesso N1 has a LoRa radio (Semtech SX1262). The Cardputer ADV can use LoRa with the
[Cap LoRa-1262](https://docs.m5stack.com/en/cap/Cap_LoRa-1262) (also SX1262) attached to its EXT bus; it is detected at startup
(the original Cardputer has no bus for it). Switch to it with RADIO on the settings screen or with `radio lora`.
Nesso N1 and Cardputer ADV stations can talk to each other over LoRa.
The packet contents and receiver processing are the same as for ESP-NOW; only the way packets are sent is different.

- **Radio conditions:** Follows the category with carrier sense of 5 ms or more for Japan's 920 MHz band specified low-power radio stations (20 mW or less, ARIB STD-T108).
  - Antenna power: Nesso N1 13 dBm (20 mW); Cap LoRa-1262 5 dBm (about 3 mW).
  - The frequencies, bandwidth and power are within the type approval of the Cap LoRa-1262 (MIC 211-251216:
    922.0-923.4 MHz, 0.2 MHz x 8 channels, 1.5-5.0 mW). Do not change them for the Cap.
  - Carrier sense for 5 ms before transmitting; if a signal of -80 dBm or more is present, wait a little and retry.
  - After each transmission (about 25 ms), pause for at least 50 ms. This category has no limit on the total transmission time per hour.
- **Modulation:** LoRa SF5 / 125 kHz bandwidth (one 200 kHz unit channel) / CR 4/5. The airtime of a 35-byte frame is about 25 ms.
- **Channels and frequencies:** The center frequency of channel n is `922.0 + 0.2 x (n - 1)` MHz (CH1 = 922.0 MHz to CH8 = 923.4 MHz). As with ESP-NOW, the channel number is also put in the packet.
- **Send interval:** Because of the required pause, the minimum interval is about 80 ms (ESP-NOW: 20 ms). The engine builds packets to send the same way as for ESP-NOW, but the LoRa task sends only the latest one at the moment it is allowed to transmit. Just before sending, it re-stamps the send time and the elapsed times in the history, so the contents match the state at that moment. The idle heartbeat is every 1 second.
- **Sender:** LoRa has no source address, so the sender's MAC address (6 bytes) is prepended to the packet. All stations broadcast to each other on the same frequency as equal peers.
- **Latency:** The default jitter delay is 250 ms (ESP-NOW: 150 ms). It takes about 0.26 seconds from pressing the key until the other station's tone sounds. It can be changed with `jitter`, but shortening it increases late arrivals (decoding errors) when packets are lost.
- **Received signal strength:** The receive path has an LNA (about 21 dB), so 20 dB is subtracted from the SX1262 RSSI and the result is shown as the value at the antenna. When units sit side by side on a desk, it saturates and shows about -20 dBm.
- Do not transmit without an antenna attached (it can damage the SX1262).

### Receiver playback

- **Time alignment:** The clocks of the sender and receiver are not synchronized. So for each packet, `receive time - send time` is computed, and the minimum over the last 4 seconds (the packet with the smallest network delay) is used as the time offset.
- **Jitter buffer:** A transition at sender time T is played back at receiver time `T + offset + 150 ms` (250 ms for LoRa). This way, even if arrival times vary, the original DOWN/UP durations are reproduced exactly.
- **Watchdog:** If the playback position passes "send time of the latest packet received + 100 ms" while still DOWN, the state is forced to UP. If a DOWN packet with the same transition ID then arrives (meaning it was just packet loss), DOWN is resumed.
- **Decoding:** Performed on the played-back timeline. The dit length is estimated adaptively; a DOWN of 2 or more dits is judged a dah. An UP of 2 or more dits is judged a character gap, and 5 or more dits a word gap.

### Task structure

Processing is split into two tasks (especially important because the ESP32-C6 is single-core).

- **`engine.cpp`:** A high-priority task with a 1 ms period. It handles key input, the keyer, sending, receive playback, tones, and decoding.
- **`loop()`:** Handles screen drawing, buttons, touch, battery (including I2C), serial commands, and saving settings.

Even if a screen transfer takes over ten milliseconds, keying timing is not disturbed.
The ESP-NOW receive callback only records the receive time and puts the packet in a queue; processing is done in the engine task.
For LoRa, a dedicated task (`net_lora.cpp`) handles carrier sense, sending, and receiving, and puts received packets in the same queue.
On the Nesso N1, the screen and LoRa share the same SPI bus, so screen transfers and LoRa operations are mutually exclusive (`hw::lockBus`).
On the Cardputer ADV, the Cap uses its own SPI bus (shared only with the microSD slot), and the RF switch is powered through the Cap's IO expander (PI4IOE5V6408, 0x43).

### Files

| File | Contents |
|---|---|
| `MorseComm.ino` | setup/loop, buttons and touch, serial commands, saving settings |
| `engine.h/.cpp` | Keying task with a 1 ms period |
| `build_info.h/.cpp` | Build date and time (the latest compile time among all sources) |
| `config.h` | Parameters (send intervals, jitter delay, watchdog, etc. Common to all boards) |
| `board.h` | Board detection and key pins |
| `protocol.h` | Packet format, sender-side transition history and send scheduler |
| `jitter.h` | Receiver-side transition reconstruction, time offset estimation, jitter buffer, watchdog |
| `keyer.h` | Debouncing, iambic keyer (A/B), automatic text keying, terminal mode send buffer and one-character-at-a-time keyer |
| `morse_decoder.h` / `morse_table.h` | Decoder / code table |
| `ui.*` | Screen drawing and interpretation of button and touch input (common to all boards) |
| `hw.h`, `hw_nesso.cpp`, `hw_m5stick.cpp` | Screen, buttons, touch, battery (Nesso / the 4 M5Unified models) |
| `keyboard_cardputer.cpp` | Cardputer keyboard (original: key matrix scanning / ADV: TCA8418) |
| `tone_out.h`, `tone_out_buzzer.cpp`, `tone_out_m5speaker.cpp` | Buzzer (Nesso, Plus2, Plus) / speaker (StickS3, Cardputer) |
| `power.h`, `power_nesso.cpp`, `power_sticks3.cpp`, `power_stickc.cpp` (Plus2 / Plus), `power_cardputer.cpp` | USB power detection, deep sleep (per board) |
| `net.h`, `net.cpp`, `net_internal.h` | Common radio code (receive queue, statistics, loss injection, radio type switching) |
| `net_espnow.cpp` | ESP-NOW (common to all boards) |
| `net_lora.cpp` | LoRa (Nesso N1, Cardputer ADV + Cap LoRa-1262. Carrier sense, pause, re-stamping of send times) |
| `peers.h` | Reception from multiple stations (per-sender playback and decoding) |
| `test/host/` | Unit tests and loss simulation that run on a Mac |

`protocol.h`, `jitter.h`, `keyer.h`, and `morse_decoder.h` do not use the Arduino API. Time is passed in as an argument.

## Testing

```sh
make -C test/host test
```

The tests cover:

- Packet encoding/decoding
- Send scheduling
- Keyer (differences between Mode A/B, memory, speed)
- Decoder (various WPM values and timing variation)

In addition, a simulation feeds 60 seconds of random keying through the channel under the following conditions.

- Random loss of 10/30/50%
- Burst loss
- Dropping only UP packets
- Delay variation of 0 to 80 ms / 250 ms
- Duplicates
- Clock drift and wrap-around
- LoRa send conditions (only the latest packet sent about every 80 ms, waiting on carrier sense) and loss

The tests confirm that the key never gets stuck down under any condition, and that the transition sequence and timing match whenever the loss is within the history length.

On real hardware, the same can be checked with test mode (`test on PARIS`) and loss injection (`loss 50`, `dropup 1`).

## Known limitations

- Under the extreme condition where all UP-state packets are lost, the last element of a character is played back longer by the watchdog time (about +80 ms). The key does not get stuck down.
- If two stations transmit at the same time on the same channel, their tones overlap, just as with real radio. The text is decoded per station, but characters are shown interleaved.
- Up to 6 stations can be tracked at once on one channel. Beyond that, the station not heard from for the longest time is replaced.
- When your station and another station are DOWN at the same time, the sidetone frequency is played.
- With LoRa, if packet loss continues 3 or more times in a row (an outage of about 0.3 seconds), late arrivals occur and decoding may be wrong. This is because the send interval is longer than with ESP-NOW.
- LoRa is half-duplex, so nothing can be received while your own station is transmitting (about 25 ms).
- ESP-NOW stations and LoRa stations cannot communicate with each other. Everyone must use the same radio type and the same channel.

## License

MIT License. See [LICENSE](LICENSE).
