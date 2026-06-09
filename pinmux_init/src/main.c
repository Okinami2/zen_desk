#include <stdio.h>
#include <string.h>

#include "board_pins.h"

static void print_usage(const char *program)
{
    printf("Usage: %s [--apply|--check|--print]\n", program);
}

int main(int argc, char *argv[])
{
    const char *operation = "--apply";

    if (argc > 2) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc == 2) {
        operation = argv[1];
    }

    if (strcmp(operation, "--print") == 0) {
        board_pins_print();
        return 0;
    }
    if (strcmp(operation, "--check") == 0) {
        if (board_pins_verify() != 0) {
            fprintf(stderr, "pinmux configuration does not match the board map\n");
            return 1;
        }
        printf("pinmux configuration verified\n");
        return 0;
    }
    if (strcmp(operation, "--apply") != 0) {
        print_usage(argv[0]);
        return 2;
    }

    board_pins_print();
    if (board_pins_apply() != 0) {
        fprintf(stderr, "failed to apply pinmux configuration\n");
        return 1;
    }
    if (board_pins_verify() != 0) {
        fprintf(stderr, "pinmux verification failed after apply\n");
        return 1;
    }
    printf("pinmux configuration applied and verified\n");
    return 0;
}
