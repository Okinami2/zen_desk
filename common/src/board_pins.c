#include "board_pins.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BOARD_DEVMEM_PATH "/dev/mem"
#define BOARD_PINMUX_LOCK_PATH "/tmp/pegasus-pinmux.lock"

static const board_pin_config g_board_pins[BOARD_PIN_COUNT] = {
    {BOARD_PIN_RADAR_TX, 8, 0x102F0138UL, 1, "UART4_TXD", "radar"},
    {BOARD_PIN_RADAR_RX, 10, 0x102F0134UL, 1, "UART4_RXD", "radar"},
    {BOARD_PIN_EC11_A, 19, 0x102F00CCUL, 0, "GPIO8_7", "ec11"},
    {BOARD_PIN_EC11_B, 21, 0x102F00D0UL, 0, "GPIO9_0", "ec11"},
    {BOARD_PIN_EC11_SW, 23, 0x102F00C8UL, 0, "GPIO8_6", "ec11"},
    {BOARD_PIN_LAMP_COLD_PWM, 32, 0x102F01ECUL, 1, "PWM0_OUT1_0_P", "lamp-cold"},
    {BOARD_PIN_VOICE_RX, 35, 0x102F0100UL, 4, "UART5_RXD", "voice"},
    {BOARD_PIN_EC11_PULLUP_ENABLE, 36, 0x102F00D8UL, 0, "GPIO0_2", "ec11-pullup"},
    {BOARD_PIN_LAMP_WARM_PWM, 37, 0x102F00DCUL, 5, "PWM0_OUT15_0_P", "lamp-warm"},
    {BOARD_PIN_VOICE_TX, 40, 0x102F0104UL, 4, "UART5_TXD", "voice"},
};

static int board_pins_validate_table(void)
{
    size_t i;
    size_t j;

    for (i = 0; i < BOARD_PIN_COUNT; i++) {
        if (g_board_pins[i].id != (board_pin_id)i ||
            g_board_pins[i].header_pin == 0 ||
            g_board_pins[i].register_addr == 0 ||
            g_board_pins[i].function > BOARD_PINMUX_MASK ||
            g_board_pins[i].signal == NULL ||
            g_board_pins[i].owner == NULL) {
            fprintf(stderr, "pinmux: invalid table entry %zu\n", i);
            return -1;
        }
        for (j = i + 1; j < BOARD_PIN_COUNT; j++) {
            if (g_board_pins[i].header_pin == g_board_pins[j].header_pin) {
                fprintf(stderr, "pinmux: physical pin %u is assigned twice\n",
                    g_board_pins[i].header_pin);
                return -1;
            }
            if (g_board_pins[i].register_addr == g_board_pins[j].register_addr) {
                fprintf(stderr, "pinmux: register 0x%08lx is assigned twice\n",
                    g_board_pins[i].register_addr);
                return -1;
            }
            if (strcmp(g_board_pins[i].signal, g_board_pins[j].signal) == 0) {
                fprintf(stderr, "pinmux: signal %s is assigned twice\n",
                    g_board_pins[i].signal);
                return -1;
            }
        }
    }

    if (BOARD_PWM_LAMP_COLD_CHANNEL == BOARD_PWM_LAMP_WARM_CHANNEL) {
        fprintf(stderr, "pinmux: lamp PWM channels conflict\n");
        return -1;
    }
    if (strcmp(BOARD_GPIO_EC11_A, BOARD_GPIO_EC11_B) == 0 ||
        strcmp(BOARD_GPIO_EC11_A, BOARD_GPIO_EC11_SW) == 0 ||
        strcmp(BOARD_GPIO_EC11_B, BOARD_GPIO_EC11_SW) == 0) {
        fprintf(stderr, "pinmux: EC11 GPIO assignments conflict\n");
        return -1;
    }
    return 0;
}

static int board_pins_lock(void)
{
    int fd = open(BOARD_PINMUX_LOCK_PATH, O_CREAT | O_RDWR, 0666);

    if (fd < 0) {
        perror("pinmux: open lock");
        return -1;
    }
    if (flock(fd, LOCK_EX) != 0) {
        perror("pinmux: lock");
        close(fd);
        return -1;
    }
    return fd;
}

static void board_pins_unlock(int fd)
{
    if (fd >= 0) {
        (void)flock(fd, LOCK_UN);
        (void)close(fd);
    }
}

static int board_pin_map_register(int mem_fd, unsigned long address,
    void **mapped_page, volatile uint32_t **reg)
{
    long page_size = sysconf(_SC_PAGESIZE);
    unsigned long page_base;
    unsigned long page_offset;
    void *mapping;

    if (page_size <= 0) {
        return -1;
    }
    page_base = address & ~((unsigned long)page_size - 1UL);
    page_offset = address - page_base;
    mapping = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE,
        MAP_SHARED, mem_fd, (off_t)page_base);
    if (mapping == MAP_FAILED) {
        fprintf(stderr, "pinmux: mmap 0x%08lx failed: %s\n",
            address, strerror(errno));
        return -1;
    }

    *mapped_page = mapping;
    *reg = (volatile uint32_t *)((unsigned char *)mapping + page_offset);
    return (int)page_size;
}

static int board_pin_read(int mem_fd, const board_pin_config *pin,
    uint32_t *value)
{
    void *mapping;
    volatile uint32_t *reg;
    int page_size = board_pin_map_register(mem_fd, pin->register_addr,
        &mapping, &reg);

    if (page_size < 0) {
        return -1;
    }
    *value = *reg;
    (void)munmap(mapping, (size_t)page_size);
    return 0;
}

static int board_pin_apply_one(int mem_fd, const board_pin_config *pin)
{
    void *mapping;
    volatile uint32_t *reg;
    uint32_t current;
    uint32_t desired;
    uint32_t verified;
    int page_size = board_pin_map_register(mem_fd, pin->register_addr,
        &mapping, &reg);

    if (page_size < 0) {
        return -1;
    }
    current = *reg;
    desired = (current & ~BOARD_PINMUX_MASK) |
        (pin->function & BOARD_PINMUX_MASK);
    if (current != desired) {
        *reg = desired;
        __sync_synchronize();
    }
    verified = *reg;
    (void)munmap(mapping, (size_t)page_size);

    if ((verified & BOARD_PINMUX_MASK) != pin->function) {
        fprintf(stderr,
            "pinmux: pin %u %s verify failed: register=0x%08x expected_func=%u\n",
            pin->header_pin, pin->signal, verified, pin->function);
        return -1;
    }
    return 0;
}

static int board_gpio_write_file(const char *path, const char *value,
    int busy_is_success)
{
    int fd = open(path, O_WRONLY);
    ssize_t length = (ssize_t)strlen(value);
    ssize_t written;

    if (fd < 0) {
        fprintf(stderr, "pinmux: open %s failed: %s\n", path, strerror(errno));
        return -1;
    }
    written = write(fd, value, (size_t)length);
    if (written != length && !(busy_is_success && errno == EBUSY)) {
        fprintf(stderr, "pinmux: write %s failed: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int board_gpio_enable_ec11_pullup(void)
{
    char direction_path[96];
    int retry;

    if (board_gpio_write_file("/sys/class/gpio/export",
        BOARD_GPIO_EC11_PULLUP_ENABLE, 1) != 0 && errno != EBUSY) {
        return -1;
    }
    (void)snprintf(direction_path, sizeof(direction_path),
        "/sys/class/gpio/gpio%s/direction", BOARD_GPIO_EC11_PULLUP_ENABLE);

    for (retry = 0; retry < 20 && access(direction_path, W_OK) != 0; retry++) {
        (void)usleep(10000);
    }
    if (board_gpio_write_file(direction_path, "high", 0) != 0) {
        return -1;
    }
    return 0;
}

static int board_gpio_verify_ec11_pullup(void)
{
    char value_path[96];
    char value = '\0';
    int fd;

    (void)snprintf(value_path, sizeof(value_path),
        "/sys/class/gpio/gpio%s/value", BOARD_GPIO_EC11_PULLUP_ENABLE);
    fd = open(value_path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "pinmux: EC11 pull-up GPIO is not exported: %s\n",
            strerror(errno));
        return -1;
    }
    if (read(fd, &value, 1) != 1) {
        fprintf(stderr, "pinmux: cannot read EC11 pull-up GPIO: %s\n",
            strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    if (value != '1') {
        fprintf(stderr, "pinmux: EC11 pull-up GPIO is not high\n");
        return -1;
    }
    return 0;
}

int board_pins_apply(void)
{
    size_t i;
    int mem_fd = -1;
    int lock_fd = -1;
    int result = -1;

    if (board_pins_validate_table() != 0) {
        return -1;
    }
    lock_fd = board_pins_lock();
    if (lock_fd < 0) {
        return -1;
    }
    mem_fd = open(BOARD_DEVMEM_PATH, O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        fprintf(stderr, "pinmux: open %s failed: %s; run as root\n",
            BOARD_DEVMEM_PATH, strerror(errno));
        goto cleanup;
    }

    for (i = 0; i < BOARD_PIN_COUNT; i++) {
        if (board_pin_apply_one(mem_fd, &g_board_pins[i]) != 0) {
            goto cleanup;
        }
    }
    if (board_gpio_enable_ec11_pullup() != 0) {
        goto cleanup;
    }
    result = 0;

cleanup:
    if (mem_fd >= 0) {
        close(mem_fd);
    }
    board_pins_unlock(lock_fd);
    return result;
}

int board_pins_verify(void)
{
    size_t i;
    int mem_fd;
    int errors = 0;

    if (board_pins_validate_table() != 0) {
        return -1;
    }
    mem_fd = open(BOARD_DEVMEM_PATH, O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        fprintf(stderr, "pinmux: open %s failed: %s; run as root\n",
            BOARD_DEVMEM_PATH, strerror(errno));
        return -1;
    }

    for (i = 0; i < BOARD_PIN_COUNT; i++) {
        uint32_t value = 0;
        if (board_pin_read(mem_fd, &g_board_pins[i], &value) != 0 ||
            (value & BOARD_PINMUX_MASK) != g_board_pins[i].function) {
            fprintf(stderr,
                "pinmux: pin %u %-18s owner=%-12s register=0x%08lx "
                "actual=%u expected=%u\n",
                g_board_pins[i].header_pin, g_board_pins[i].signal,
                g_board_pins[i].owner, g_board_pins[i].register_addr,
                value & BOARD_PINMUX_MASK, g_board_pins[i].function);
            errors++;
        }
    }
    if (board_gpio_verify_ec11_pullup() != 0) {
        errors++;
    }
    close(mem_fd);
    return errors == 0 ? 0 : -1;
}

void board_pins_print(void)
{
    size_t i;

    printf("Pegasus 40-pin allocation:\n");
    for (i = 0; i < BOARD_PIN_COUNT; i++) {
        printf("  pin=%2u reg=0x%08lx func=%u signal=%-18s owner=%s\n",
            g_board_pins[i].header_pin, g_board_pins[i].register_addr,
            g_board_pins[i].function, g_board_pins[i].signal,
            g_board_pins[i].owner);
    }
    printf("  EC11 GPIO: A=%s B=%s SW=%s pullup_enable=%s\n",
        BOARD_GPIO_EC11_A, BOARD_GPIO_EC11_B, BOARD_GPIO_EC11_SW,
        BOARD_GPIO_EC11_PULLUP_ENABLE);
    printf("  lamp PWM: cold=%d warm=%d\n",
        BOARD_PWM_LAMP_COLD_CHANNEL, BOARD_PWM_LAMP_WARM_CHANNEL);
    printf("  UART: radar=%s voice=%s\n",
        BOARD_RADAR_UART_DEVICE, BOARD_VOICE_UART_DEVICE);
}
