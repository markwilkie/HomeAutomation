# Adding a New Blind (Generating a New Channel Array)

This guide explains how to add another blind (e.g. blind 7) to the controller in
the future, the same way blind 6 was created. No physical remote or capture is
required — the A-OK protocol is fully decoded, so any of the 16 channels can be
generated analytically and is guaranteed to transmit.

## 1. The A-OK frame format

Every command is a **72-bit / 9-byte** frame. Each bit is encoded as a
pulse-pair (a HIGH pulse followed by a LOW pulse); the bit is `1` when HIGH > LOW.

| Byte | Value          | Meaning                                          |
|------|----------------|--------------------------------------------------|
| 0    | `03`           | Remote ID (constant)                             |
| 1    | `C0`           | Remote ID (constant)                             |
| 2    | `C1`           | Remote ID (constant)                             |
| 3-4  | one-hot 16-bit | **Channel** — blind *N* sets bit *N-1*           |
| 5    | `01`           | constant                                         |
| 6    | `00`           | constant                                         |
| 7    | command        | DOWN = `02`, UP = `01`                           |
| 8    | checksum       | `sum(byte0..byte7) & 0xFF`                       |

### Channel encoding (the only part that changes per blind)

The channel is a **16-bit one-hot** value spread across bytes 3 (high) and 4 (low):

```
onehot = 1 << (N - 1)
byte3  = (onehot >> 8) & 0xFF     # high byte
byte4  =  onehot       & 0xFF     # low byte
```

| Blind | one-hot  | byte3 | byte4 |
|-------|----------|-------|-------|
| 1     | 0x0001   | 00    | 01    |
| 2     | 0x0002   | 00    | 02    |
| 5     | 0x0010   | 00    | 10    |
| 6     | 0x0020   | 00    | 20    |
| 7     | 0x0040   | 00    | 40    |
| 8     | 0x0080   | 00    | 80    |
| 9     | 0x0100   | 01    | 00    |
| 16    | 0x8000   | 80    | 00    |

So the protocol supports a maximum of **16 blinds**.

#### Worked example — blind 6 DOWN

```
byte3:byte4 = 0x0020   ->  byte3=00, byte4=20
frame  = 03 C0 C1 00 20 01 00 02 ??
checksum = (03+C0+C1+00+20+01+00+02) & 0xFF = 0x1A7 & 0xFF = A7
frame  = 03 C0 C1 00 20 01 00 02 A7      (UP is the same with byte7=01 -> A6)
```

## 2. Generate the timing arrays

Use [gen_blind.py](gen_blind.py). It takes the proven, even-aligned `blind2`
capture and swaps only the pulse-pairs whose bits differ from the target frame,
leaving every physical pulse-pair clean. Before emitting anything it re-derives
all 5 known channels and aborts if the model can't reproduce them, so a clean run
is self-validating.

```powershell
# Print the two C arrays for the new blind (example: blind 7)
python gen_blind.py 7

# Or append straight into the data header, then delete the trailing #endif
# that is now in the middle of the file and re-add one at the very end.
python gen_blind.py 7 >> blind_data.h
```

The script prints `blind7_down[511]` and `blind7_up[511]` ready to paste, each
with a header comment showing the 9 frame bytes it encodes.

> The generator validates that **every** frame in the output (a) decodes back to
> the intended 9 bytes and (b) has only clean short/long pulse-pairs. If either
> check fails it exits with an error instead of producing a bad array.

## 3. Wire the new blind into `blind_controller.ino`

After pasting the new arrays into [blind_data.h](blind_data.h):

1. **Bump the max** — increase `MAX_BLINDS` and the validation range
   (`blind_number < 1 || blind_number > MAX_BLINDS`, message text).
2. **Add a `BlindCodes` entry** for the new blind (raw-data blinds use
   `{0x000000, 0x000000}` since they transmit from the raw arrays, not codes).
3. **Add the `control_blind` cases** that select the new raw arrays, e.g.:
   ```cpp
   else if (blind_number == 7 && action == "down") {
       sendRawData(blind7_down, 511);
   }
   else if (blind_number == 7 && action == "up") {
       sendRawData(blind7_up, 511);
   }
   ```
4. **(Optional) Add it to a group** in `callback()` — e.g. include
   `control_blind(7, action)` alongside the existing group members and update
   the group's print/label.

## 4. Build, upload, test

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32doit-devkit-v1 .
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32doit-devkit-v1 .
```

Then publish an MQTT command for the new blind and confirm it moves. If nothing
moves but the code is correct, check the **RF TX wiring/pin first** — that was the
root cause of the only real failure when blind 6 was added (`RF_TX_PIN = 14` in
code is correct; the physical wiring was wrong).

## 5. Why this is reliable

- The encoding (channel one-hot, command, checksum) was confirmed byte-for-byte
  against **all 10** known-working frames (5 channels × up/down).
- Regenerating each known channel from the `blind2` base reproduces its real
  bytes and clean pulse-pairs exactly, so the same procedure for a new channel is
  equally trustworthy.
- The base array must be **even-aligned** (its first frame starts at an even flat
  index) so pair-swaps land on whole protocol bits. `blind2` satisfies this; the
  generator asserts it.
