#include "protocol.h"

#include <glib.h>

const uint8_t DAC_ADDR[DACS_PER_BANK] = { 0x48, 0x49, 0x4A, 0x4B };

uint16_t voltage_to_code(double v) {
    if (v <= 0.0) return 0x0000;
    if (v >= VOLTAGE_FULL_SCALE) return 0xFFFF;
    uint32_t code = (uint32_t)(v / VOLTAGE_FULL_SCALE * 65536.0 + 0.5);
    return (uint16_t)(code > 0xFFFF ? 0xFFFF : code);
}

void protocol_send_voltage(serial_fd_t fd, uint8_t reg, int channel, uint16_t value) {
    uint8_t bank = (uint8_t)(channel / DACS_PER_BANK);
    uint8_t chip = (uint8_t)(channel % DACS_PER_BANK);

    uint8_t msg[6];
    msg[0] = DAC_CMD_START;      /* this byte matters for the firmware */
    msg[1] = bank;
    msg[2] = DAC_ADDR[chip];     /* address of the DAC within the bank */
    msg[3] = reg;
    msg[4] = (uint8_t)(value >> 8);
    msg[5] = (uint8_t)(value & 0xFF);

    g_print("Sending 0x: %02X %02X %02X %02X %02X\n",
            msg[1], msg[2], msg[3], msg[4], msg[5]);
    serial_write(fd, msg, sizeof(msg));
}

void protocol_send_reset(serial_fd_t fd) {
    uint8_t msg[2] = { RST_CMD_START, 0x00 };
    g_print("Pulling down RST pin.\n");
    serial_write(fd, msg, sizeof(msg));
}

void protocol_send_spi(serial_fd_t fd, const uint8_t *bytes, int count) {
    uint8_t msg[2 + 3];
    if (count < 0) count = 0;
    if (count > 3) count = 3;

    msg[0] = SPI_CMD_START;
    msg[1] = (uint8_t)count;
    for (int i = 0; i < count; i++) msg[2 + i] = bytes[i];

    g_print("Sending 0x:");
    for (int i = 0; i < count; i++) g_print(" %02X", msg[2 + i]);
    g_print("\n");

    serial_write(fd, msg, (size_t)(2 + count));
}
