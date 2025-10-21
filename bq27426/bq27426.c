#include "bq27426.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static int read_word(int fd, uint8_t reg)
{
    uint8_t buf[2];
    if (write(fd, &reg, 1) != 1) return -1;
    if (read(fd, buf, 2) != 2) return -1;
    return (buf[0] | (buf[1] << 8));
}

static double kelvin_to_celsius(int value)
{
    return (value - 2731) / 10.0;
}

int bq27426_read_data(const char *i2c_bus, int addr, bq27426_data_t *d)
{
    int fd = open(i2c_bus, O_RDWR);
    if (fd < 0) { perror("open"); return -1; }
    if (ioctl(fd, I2C_SLAVE, addr) < 0) { perror("ioctl"); close(fd); return -1; }

    memset(d, 0, sizeof(*d));

    d->voltage          = read_word(fd, BQ27426_REG_VOLT);
    d->current          = (int16_t)read_word(fd, BQ27426_REG_AVG_CURRENT);
    d->power            = (int16_t)read_word(fd, BQ27426_REG_AVG_POWER);
    d->soc             = read_word(fd, BQ27426_REG_SOC) & 0xFF;
    d->soh             = read_word(fd, BQ27426_REG_SOH) & 0xFF;
    d->nominal_capacity= read_word(fd, BQ27426_REG_NOM_CAPACITY);
    d->available_capacity = read_word(fd, BQ27426_REG_AVAIL_CAPACITY);
    d->remaining_capacity = read_word(fd, BQ27426_REG_REM_CAPACITY);
    d->full_capacity   = read_word(fd, BQ27426_REG_FULL_CAPACITY);
    d->temperature_cell  = kelvin_to_celsius(read_word(fd, BQ27426_REG_TEMP));
    d->temperature_internal = kelvin_to_celsius(read_word(fd, BQ27426_REG_INT_TEMP));
    d->flags               = read_word(fd, BQ27426_REG_FLAGS);

    close(fd);
    return 0;
}

void bq27426_print_data(const bq27426_data_t *d)
{
    printf("=== BQ27426 Fuel Gauge ===\n");
    printf("Voltage              : %d mV\n", d->voltage);
    printf("Avg Current          : %d mA\n", d->current);
    printf("Avg Power            : %d mW\n", d->power);
    printf("SOC (Charge)         : %d %%\n", d->soc);
    printf("SOH (Health)         : %d %%\n", d->soh);
    printf("Nominal Capacity     : %d mAh\n", d->nominal_capacity);
    printf("Available Capacity   : %d mAh\n", d->available_capacity);
    printf("Remaining Capacity   : %d mAh\n", d->remaining_capacity);
    printf("Full Charge Capacity : %d mAh\n", d->full_capacity);
    printf("Cell Temperature     : %.1f °C\n", d->temperature_cell);
    printf("Internal Temp        : %.1f °C\n", d->temperature_internal);
    printf("Flags                : 0x%04X\n", d->flags);
    printf("==========================>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
}
