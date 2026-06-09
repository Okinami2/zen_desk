# Pegasus 40-Pin Allocation

The authoritative software mapping is `common/src/board_pins.c`.
Do not add pinmux register writes or duplicate GPIO/PWM numbers in services.

| Physical pin | Register | Function | Signal | Owner |
|---:|---:|---:|---|---|
| 8 | `0x102F0138` | 1 | `UART4_TXD` | radar |
| 10 | `0x102F0134` | 1 | `UART4_RXD` | radar |
| 19 | `0x102F00CC` | 0 | `GPIO8_7` / GPIO 71 | EC11 A |
| 21 | `0x102F00D0` | 0 | `GPIO9_0` / GPIO 72 | EC11 B |
| 23 | `0x102F00C8` | 0 | `GPIO8_6` / GPIO 70 | EC11 switch |
| 32 | `0x102F01EC` | 1 | `PWM0_OUT1_0_P` / PWM 1 | cold lamp |
| 35 | `0x102F0100` | 4 | `UART5_RXD` | voice module |
| 36 | `0x102F00D8` | 0 | `GPIO0_2` / GPIO 2 high | EC11 pull-up supply |
| 37 | `0x102F00DC` | 5 | `PWM0_OUT15_0_P` / PWM 15 | warm lamp |
| 40 | `0x102F0104` | 4 | `UART5_TXD` | voice module |

## Runtime

Apply and verify the board mapping as root:

```sh
out/bin/pinmux_init --apply
out/bin/pinmux_init --check
```

Print the compiled allocation without touching hardware:

```sh
out/bin/pinmux_init --print
```

`scripts/start_all.sh` and `scripts/run_qt_display.sh` apply this mapping
before starting services. `radar_service` and `device_service` also apply it
idempotently when launched directly.

## Ownership Rules

- Physical header pins and pinmux registers must be unique.
- EC11 GPIO values come from `board_pins.h`.
- Lamp PWM 1 is cold and PWM 15 is warm.
- Radar uses `/dev/ttyAMA4`.
- Voice UART5 is reserved as `/dev/ttyAMA5`.
- GPIO 2 remains exported and driven high while the board is running.
- Pinmux writes are serialized by `/tmp/pegasus-pinmux.lock`.

The utility updates only the low three function-select bits and preserves
the remaining register bits.
