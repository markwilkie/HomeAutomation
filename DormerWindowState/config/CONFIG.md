# Sensor Configuration

All options live under **Dormer Window State Configuration** in
`idf.py menuconfig`, backed by `main/Kconfig.projbuild`.

| Option | Default | Description |
|---|---|---|
| `WINDOW_SENSOR_GPIO` | `4` | Digital input GPIO wired to the reed switch. |
| `WINDOW_SENSOR_ACTIVE_LOW` | `y` | `y` if the pin reads LOW when the window is closed (switch shorts to GND, internal pull-up enabled). Set to `n` if your switch instead pulls the pin HIGH when closed. |
| `WINDOW_SENSOR_DEBOUNCE_MS` | `50` | Milliseconds the pin must hold a stable level after an edge before the new state is trusted and reported to Matter. Increase for mechanically noisy switches. |

## Matter mapping

| Matter element | Value |
|---|---|
| Device type | Contact Sensor (`0x0015`) |
| Cluster | Boolean State (`0x0045`) |
| Attribute | `StateValue` (`0x0000`) — `true` = open, `false` = closed |

## Choosing a GPIO

Any free input-capable GPIO on the ESP32-C6 works. Avoid the strapping pins
(GPIO 4, 5, 8, 9, 15) if you also need reliable boot behavior with the switch
in an arbitrary position — pick a different pin in menuconfig if that's a
concern for your wiring, or just leave the default (GPIO4) if it isn't.

## Changing wiring polarity

If you wire the reed switch to 3.3V instead of GND (pull-down instead of
pull-up), set `WINDOW_SENSOR_ACTIVE_LOW=n`. The driver flips both the pull
resistor selection and the open/closed interpretation automatically — no
code changes needed.
