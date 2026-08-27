#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#include "config.h"
#include "serial.h"

/*
 * Wire format spoken to the microcontroller. The GUI never touches I2C or
 * SPI itself: it frames a command, and the firmware decides which bus to
 * drive based on the leading byte.
 */

/* DAC70501 register map. */
enum {
    REG_SYNC    = 0x01,
    REG_CONFIG  = 0x03,
    REG_GAIN    = 0x04,
    REG_TRIGGER = 0x05,
    REG_DAC     = 0x08
};

/* Leading byte: tells the firmware how to read the bytes that follow. */
enum {
    DAC_CMD_START = 0x00, /* write a voltage value to a DAC   */
    RST_CMD_START = 0x01, /* pull the Reset pin down          */
    SPI_CMD_START = 0x02  /* forward raw bytes over SPI       */
};

/* Payloads written to REG_CONFIG to power a DAC up or down. */
enum {
    DAC_POWER_ON  = 0x0000,
    DAC_POWER_OFF = 0x0001
};

/* I2C addresses of the four DACs within a bank. */
extern const uint8_t DAC_ADDR[DACS_PER_BANK];

/* Maps a voltage onto a 16-bit DAC code using the fixed hardware full
 * scale. Values outside [0, VOLTAGE_FULL_SCALE] saturate. */
uint16_t voltage_to_code(double v);

/* [DAC_CMD_START][bank][dac addr][reg][value hi][value lo] */
void protocol_send_voltage(serial_fd_t fd, uint8_t reg, int channel, uint16_t value);

/* [RST_CMD_START][0x00] */
void protocol_send_reset(serial_fd_t fd);

/* [SPI_CMD_START][count][byte 0]..[byte count-1] */
void protocol_send_spi(serial_fd_t fd, const uint8_t *bytes, int count);

#endif /* PROTOCOL_H */
