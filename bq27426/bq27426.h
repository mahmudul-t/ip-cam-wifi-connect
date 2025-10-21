#ifndef _BQ27426_H_
#define _BQ27426_H_

#include <stdint.h>

// I2C bus and address
#define BQ27426_I2C_BUS   "/dev/i2c-2"
#define BQ27426_ADDR      0x55

// Register map (standard commands)
#define BQ27426_REG_TEMP              0x02
#define BQ27426_REG_VOLT              0x04
#define BQ27426_REG_FLAGS             0x06
#define BQ27426_REG_NOM_CAPACITY      0x08
#define BQ27426_REG_AVAIL_CAPACITY    0x0A
#define BQ27426_REG_REM_CAPACITY      0x0C
#define BQ27426_REG_FULL_CAPACITY     0x0E
#define BQ27426_REG_AVG_CURRENT       0x10
#define BQ27426_REG_AVG_POWER         0x18
#define BQ27426_REG_SOC               0x1C
#define BQ27426_REG_INT_TEMP          0x1E
#define BQ27426_REG_SOH               0x20

// Structure to store readings
typedef struct {
    int voltage;
    int current;
    int power;
    int soc;
    int soh;
    int nominal_capacity;
    int available_capacity;
    int remaining_capacity;
    int full_capacity;
    double temperature_cell;
    double temperature_internal;
    int flags;
} bq27426_data_t;

// Function declarations
int bq27426_read_data(const char *i2c_bus, int addr, bq27426_data_t *data);
void bq27426_print_data(const bq27426_data_t *data);

#endif
