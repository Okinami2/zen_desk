#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_PINMUX_MASK 0x7U

#define BOARD_RADAR_UART_DEVICE "/dev/ttyAMA4"
#define BOARD_VOICE_UART_DEVICE "/dev/ttyAMA5"

#define BOARD_GPIO_EC11_A "71"
#define BOARD_GPIO_EC11_B "72"
#define BOARD_GPIO_EC11_SW "70"
#define BOARD_GPIO_EC11_PULLUP_ENABLE "2"

#define BOARD_PWM_LAMP_COLD_CHANNEL 1
#define BOARD_PWM_LAMP_WARM_CHANNEL 15

typedef enum {
    BOARD_PIN_RADAR_TX = 0,
    BOARD_PIN_RADAR_RX,
    BOARD_PIN_EC11_A,
    BOARD_PIN_EC11_B,
    BOARD_PIN_EC11_SW,
    BOARD_PIN_LAMP_COLD_PWM,
    BOARD_PIN_VOICE_RX,
    BOARD_PIN_EC11_PULLUP_ENABLE,
    BOARD_PIN_LAMP_WARM_PWM,
    BOARD_PIN_VOICE_TX,
    BOARD_PIN_COUNT
} board_pin_id;

typedef struct {
    board_pin_id id;
    unsigned int header_pin;
    unsigned long register_addr;
    unsigned int function;
    const char *signal;
    const char *owner;
} board_pin_config;

int board_pins_apply(void);
int board_pins_verify(void);
void board_pins_print(void);

#ifdef __cplusplus
}
#endif

#endif
