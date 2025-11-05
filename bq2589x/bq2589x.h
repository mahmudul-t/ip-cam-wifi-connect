#ifndef BQ2589X_H
#define BQ2589X_H

#include <stdint.h>
#include <stdbool.h>
#include "bq2589x_reg.h"

#define BQ25895_ADDR          0x6A
#define BQ2589X_OK            0
#define BQ2589X_ERR           1

typedef enum {
    BQ2589X_VBUS_NONE = 0,
    BQ2589X_VBUS_USB_SDP,
    BQ2589X_VBUS_USB_CDP,
    BQ2589X_VBUS_USB_DCP,
    BQ2589X_VBUS_MAXC,
    BQ2589X_VBUS_UNKNOWN,
    BQ2589X_VBUS_NONSTAND,
    BQ2589X_VBUS_OTG,
    BQ2589X_VBUS_TYPE_NUM
} bq2589x_vbus_type;

typedef enum {
    BQ25890 = 0x03,
    BQ25892 = 0x00,
    BQ25895 = 0x07
} bq2589x_part_no;

/* --------------------------------------------------------------- */
/* Device handle – one per BQ2589x instance                         */
typedef struct {
    int      fd;        /* /dev/i2c-X file descriptor */
    uint8_t  addr;      /* 7-bit I2C address          */
} bq2589x_dev;

/* --------------------------------------------------------------- */
/* Public API – function prototypes (same semantics as C++ version) */
int  bq2589x_init(bq2589x_dev *dev, uint8_t i2c_addr);
void bq2589x_deinit(bq2589x_dev *dev);

int  bq2589x_read_reg (bq2589x_dev *dev, uint8_t reg, uint8_t *data);
int  bq2589x_write_reg(bq2589x_dev *dev, uint8_t reg, uint8_t data);
int  bq2589x_update_bits(bq2589x_dev *dev, uint8_t reg,
                         uint8_t mask, uint8_t data);

bq2589x_vbus_type bq2589x_get_vbus_type(bq2589x_dev *dev);

int bq2589x_enable_otg (bq2589x_dev *dev);
int bq2589x_disable_otg(bq2589x_dev *dev);
int bq2589x_set_otg_volt(bq2589x_dev *dev, uint16_t volt_mV);
int bq2589x_set_otg_current(bq2589x_dev *dev, int curr_mA);

int bq2589x_enable_charger (bq2589x_dev *dev);
int bq2589x_disable_charger(bq2589x_dev *dev);

int bq2589x_adc_start(bq2589x_dev *dev, bool oneshot);
int bq2589x_adc_stop (bq2589x_dev *dev);
int bq2589x_adc_read_battery_volt(bq2589x_dev *dev);   /* mV */
int bq2589x_adc_read_sys_volt    (bq2589x_dev *dev);   /* mV */
int bq2589x_adc_read_vbus_volt   (bq2589x_dev *dev);   /* mV */
int bq2589x_adc_read_temperature (bq2589x_dev *dev);   /* 0.1°C */
int bq2589x_adc_read_charge_current(bq2589x_dev *dev); /* mA */

int bq2589x_set_charge_current   (bq2589x_dev *dev, int curr_mA);
int bq2589x_set_term_current     (bq2589x_dev *dev, int curr_mA);
int bq2589x_set_prechg_current   (bq2589x_dev *dev, int curr_mA);
int bq2589x_set_chargevoltage    (bq2589x_dev *dev, int volt_mV);
int bq2589x_set_input_volt_limit (bq2589x_dev *dev, int volt_mV);
int bq2589x_set_input_current_limit(bq2589x_dev *dev, int curr_mA);
int bq2589x_set_vindpm_offset    (bq2589x_dev *dev, int offset_mV);

int bq2589x_get_charging_status(bq2589x_dev *dev);   /* 0-3 or 4=error */

int bq2589x_set_watchdog_timer(bq2589x_dev *dev, uint8_t timeout_s);
int bq2589x_disable_watchdog_timer(bq2589x_dev *dev);
int bq2589x_reset_watchdog_timer(bq2589x_dev *dev);

int bq2589x_force_dpdm(bq2589x_dev *dev);
int bq2589x_reset_chip(bq2589x_dev *dev);
int bq2589x_enter_ship_mode(bq2589x_dev *dev);
int bq2589x_enter_hiz_mode(bq2589x_dev *dev);
int bq2589x_exit_hiz_mode (bq2589x_dev *dev);
int bq2589x_get_hiz_mode  (bq2589x_dev *dev, uint8_t *state);

int bq2589x_pumpx_enable(bq2589x_dev *dev, int enable);
int bq2589x_pumpx_increase_volt(bq2589x_dev *dev);
int bq2589x_pumpx_increase_volt_done(bq2589x_dev *dev);
int bq2589x_pumpx_decrease_volt(bq2589x_dev *dev);
int bq2589x_pumpx_decrease_volt_done(bq2589x_dev *dev);

int bq2589x_force_ico(bq2589x_dev *dev);
int bq2589x_check_force_ico_done(bq2589x_dev *dev);

int bq2589x_enable_term(bq2589x_dev *dev, bool enable);
int bq2589x_enable_auto_dpdm(bq2589x_dev *dev, bool enable);
int bq2589x_use_absolute_vindpm(bq2589x_dev *dev, bool enable);
int bq2589x_enable_ico(bq2589x_dev *dev, bool enable);

int bq2589x_read_idpm_limit(bq2589x_dev *dev);   /* mA */

bool bq2589x_is_charge_done(bq2589x_dev *dev);

int bq2589x_init_device(bq2589x_dev *dev);
int bq2589x_detect_device(bq2589x_dev *dev,
                          bq2589x_part_no *part_no, int *revision);

int bq2589x_enable_max_charge(bq2589x_dev *dev, bool enable);

#endif /* BQ2589X_H */