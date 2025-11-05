/* bq2589x.c – Linux userspace driver for BQ2589x on T23 (I2C1) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include "bq2589x.h"

/* ----------------------------------------------------------------- */
static int i2c_open(uint8_t bus)
{
    char path[20];
    snprintf(path, sizeof(path), "/dev/i2c-%u", bus);
    int fd = open(path, O_RDWR);
    if (fd < 0) 
    {
        perror("open i2c");
    }
    return fd;
}

/* ----------------------------------------------------------------- */
int bq2589x_init(bq2589x_dev *dev, uint8_t i2c_addr)
{
    if (!dev) return BQ2589X_ERR;

    dev->fd = i2c_open(1);                 /* I2C1 on T23 */
    if (dev->fd < 0) return BQ2589X_ERR;

    dev->addr = i2c_addr;

    if (ioctl(dev->fd, I2C_SLAVE, dev->addr) < 0) {
        perror("ioctl I2C_SLAVE");
        close(dev->fd);
        dev->fd = -1;
        return BQ2589X_ERR;
    }
    return bq2589x_reset_chip(dev);        /* same as original begin() */
}

void bq2589x_deinit(bq2589x_dev *dev)
{
    if (dev && dev->fd >= 0) 
    {
        close(dev->fd);
        dev->fd = -1;
    }
}

/* ----------------------------------------------------------------- */
int bq2589x_read_reg(bq2589x_dev *dev, uint8_t reg, uint8_t *data)
{
    if (!dev || dev->fd < 0 || !data) return BQ2589X_ERR;

    if (write(dev->fd, &reg, 1) != 1) 
    {
        perror("i2c write reg");
        return BQ2589X_ERR;
    }
    if (read(dev->fd, data, 1) != 1) 
    {
        perror("i2c read data");
        return BQ2589X_ERR;
    }
    return BQ2589X_OK;
}

int bq2589x_write_reg(bq2589x_dev *dev, uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = { reg, data };
    if (!dev || dev->fd < 0) return BQ2589X_ERR;

    if (write(dev->fd, buf, 2) != 2) 
    {
        perror("i2c write reg+data");
        return BQ2589X_ERR;
    }
    return BQ2589X_OK;
}

/* ----------------------------------------------------------------- */
int bq2589x_update_bits(bq2589x_dev *dev, uint8_t reg,
                        uint8_t mask, uint8_t data)
{
    uint8_t tmp;
    int ret = bq2589x_read_reg(dev, reg, &tmp);
    if (ret) return ret;

    tmp &= ~mask;
    tmp |= (data & mask);
    return bq2589x_write_reg(dev, reg, tmp);
}

/* ----------------------------------------------------------------- */
bq2589x_vbus_type bq2589x_get_vbus_type(bq2589x_dev *dev)
{
    uint8_t val;
    if (bq2589x_read_reg(dev, BQ2589X_REG_0B, &val))
        return BQ2589X_VBUS_UNKNOWN;

    val = (val & BQ2589X_VBUS_STAT_MASK) >> BQ2589X_VBUS_STAT_SHIFT;
    return (bq2589x_vbus_type)val;
}

/* ----------------------------------------------------------------- */
int bq2589x_enable_otg(bq2589x_dev *dev)
{
    uint8_t v = BQ2589X_OTG_ENABLE << BQ2589X_OTG_CONFIG_SHIFT;
    return bq2589x_update_bits(dev, BQ2589X_REG_03,
                               BQ2589X_OTG_CONFIG_MASK, v);
}
int bq2589x_disable_otg(bq2589x_dev *dev)
{
    uint8_t v = BQ2589X_OTG_DISABLE << BQ2589X_OTG_CONFIG_SHIFT;
    return bq2589x_update_bits(dev, BQ2589X_REG_03,
                               BQ2589X_OTG_CONFIG_MASK, v);
}

/* ----------------------------------------------------------------- */
int bq2589x_set_otg_volt(bq2589x_dev *dev, uint16_t volt_mV)
{
    if (volt_mV < BQ2589X_BOOSTV_BASE)
        volt_mV = BQ2589X_BOOSTV_BASE;
    if (volt_mV > BQ2589X_BOOSTV_BASE +
                 ((BQ2589X_BOOSTV_MASK>>BQ2589X_BOOSTV_SHIFT)*BQ2589X_BOOSTV_LSB))
        volt_mV = BQ2589X_BOOSTV_BASE +
                  ((BQ2589X_BOOSTV_MASK>>BQ2589X_BOOSTV_SHIFT)*BQ2589X_BOOSTV_LSB);

    uint8_t v = ((volt_mV - BQ2589X_BOOSTV_BASE) / BQ2589X_BOOSTV_LSB)
                << BQ2589X_BOOSTV_SHIFT;
    return bq2589x_update_bits(dev, BQ2589X_REG_0A, BQ2589X_BOOSTV_MASK, v);
}

int bq2589x_set_otg_current(bq2589x_dev *dev, int curr_mA)
{
    uint8_t v;
    switch (curr_mA) {
        case  500: v = BQ2589X_BOOST_LIM_500MA; break;
        case  700: v = BQ2589X_BOOST_LIM_700MA; break;
        case 1100: v = BQ2589X_BOOST_LIM_1100MA; break;
        case 1300: v = BQ2589X_BOOST_LIM_1300MA; break;
        case 1600: v = BQ2589X_BOOST_LIM_1600MA; break;
        case 1800: v = BQ2589X_BOOST_LIM_1800MA; break;
        case 2100: v = BQ2589X_BOOST_LIM_2100MA; break;
        case 2400: v = BQ2589X_BOOST_LIM_2400MA; break;
        default:   v = BQ2589X_BOOST_LIM_1300MA; break;
    }
    return bq2589x_update_bits(dev, BQ2589X_REG_0A,
                               BQ2589X_BOOST_LIM_MASK,
                               v << BQ2589X_BOOST_LIM_SHIFT);
}

/* ----------------------------------------------------------------- */
int bq2589x_enable_charger(bq2589x_dev *dev)
{
    uint8_t v = BQ2589X_CHG_ENABLE << BQ2589X_CHG_CONFIG_SHIFT;
    return bq2589x_update_bits(dev, BQ2589X_REG_03,
                               BQ2589X_CHG_CONFIG_MASK, v);
}
int bq2589x_disable_charger(bq2589x_dev *dev)
{
    uint8_t v = BQ2589X_CHG_DISABLE << BQ2589X_CHG_CONFIG_SHIFT;
    return bq2589x_update_bits(dev, BQ2589X_REG_03,
                               BQ2589X_CHG_CONFIG_MASK, v);
}

/* ----------------------------------------------------------------- */
int bq2589x_adc_start(bq2589x_dev *dev, bool oneshot)
{
    uint8_t val;
    int ret = bq2589x_read_reg(dev, BQ2589X_REG_02, &val);
    if (ret) return ret;

    if (((val & BQ2589X_CONV_RATE_MASK)>>BQ2589X_CONV_RATE_SHIFT)== BQ2589X_ADC_CONTINUE_ENABLE)
    {
        printf("[BQ CGR] alreay continous\n");
        return BQ2589X_OK;   /* already continuous */

    }
        
    if (oneshot)
    {
        return bq2589x_update_bits(dev, BQ2589X_REG_02,BQ2589X_CONV_START_MASK,BQ2589X_CONV_START << BQ2589X_CONV_START_SHIFT);
    }
    else
    {
        return bq2589x_update_bits(dev, BQ2589X_REG_02, BQ2589X_CONV_RATE_MASK, BQ2589X_ADC_CONTINUE_ENABLE <<BQ2589X_CONV_RATE_SHIFT);

    }
}


int bq2589x_adc_stop(bq2589x_dev *dev)
{
    return bq2589x_update_bits(dev, BQ2589X_REG_02,
                               BQ2589X_CONV_RATE_MASK,
                               BQ2589X_ADC_CONTINUE_DISABLE <<
                               BQ2589X_CONV_RATE_SHIFT);
}

/* ----------------------------------------------------------------- */
#define ADC_READ(reg, base, mask, shift, lsb) \
    do { \
        uint8_t v; \
        if (bq2589x_read_reg(dev, reg, &v)) return BQ2589X_ERR; \
        return (int)(base + (((v & mask) >> shift) * lsb)); \
    } while (0)




int bq2589x_adc_read_battery_volt(bq2589x_dev *dev)
{ 
    ADC_READ(BQ2589X_REG_0E, BQ2589X_BATV_BASE, BQ2589X_BATV_MASK, BQ2589X_BATV_SHIFT, BQ2589X_BATV_LSB); 
}
int bq2589x_adc_read_sys_volt(bq2589x_dev *dev)
{ 
    ADC_READ(BQ2589X_REG_0F, BQ2589X_SYSV_BASE, BQ2589X_SYSV_MASK, BQ2589X_SYSV_SHIFT, BQ2589X_SYSV_LSB); 
}

int bq2589x_adc_read_vbus_volt(bq2589x_dev *dev)
{ 
    ADC_READ(BQ2589X_REG_11, BQ2589X_VBUSV_BASE,BQ2589X_VBUSV_MASK, BQ2589X_VBUSV_SHIFT, BQ2589X_VBUSV_LSB); 
}

int bq2589x_adc_read_temperature(bq2589x_dev *dev)
{ 
    ADC_READ(BQ2589X_REG_10, BQ2589X_TSPCT_BASE,BQ2589X_TSPCT_MASK, BQ2589X_TSPCT_SHIFT, (int)(BQ2589X_TSPCT_LSB*10)); 
}

int bq2589x_adc_read_charge_current(bq2589x_dev *dev)
{ 
    ADC_READ(BQ2589X_REG_12, BQ2589X_ICHGR_BASE, BQ2589X_ICHGR_MASK, BQ2589X_ICHGR_SHIFT, BQ2589X_ICHGR_LSB); 
}

/* ----------------------------------------------------------------- */
int bq2589x_set_charge_current(bq2589x_dev *dev, int curr_mA)
{
    uint8_t v = (curr_mA - BQ2589X_ICHG_BASE) / BQ2589X_ICHG_LSB;
    return bq2589x_update_bits(dev, BQ2589X_REG_04,BQ2589X_ICHG_MASK, v << BQ2589X_ICHG_SHIFT);
}

int bq2589x_set_term_current(bq2589x_dev *dev, int curr_mA)
{
    uint8_t v = (curr_mA - BQ2589X_ITERM_BASE) / BQ2589X_ITERM_LSB;
    return bq2589x_update_bits(dev, BQ2589X_REG_05,
                               BQ2589X_ITERM_MASK,
                               v << BQ2589X_ITERM_SHIFT);
}
int bq2589x_set_prechg_current(bq2589x_dev *dev, int curr_mA)
{
    uint8_t v = (curr_mA - BQ2589X_IPRECHG_BASE) / BQ2589X_IPRECHG_LSB;
    return bq2589x_update_bits(dev, BQ2589X_REG_05,
                               BQ2589X_IPRECHG_MASK,
                               v << BQ2589X_IPRECHG_SHIFT);
}
int bq2589x_set_chargevoltage(bq2589x_dev *dev, int volt_mV)
{
    uint8_t v = (volt_mV - BQ2589X_VREG_BASE) / BQ2589X_VREG_LSB;
    return bq2589x_update_bits(dev, BQ2589X_REG_06,
                               BQ2589X_VREG_MASK,
                               v << BQ2589X_VREG_SHIFT);
}
int bq2589x_set_input_volt_limit(bq2589x_dev *dev, int volt_mV)
{
    uint8_t v = (volt_mV - BQ2589X_VINDPM_BASE) / BQ2589X_VINDPM_LSB;
    return bq2589x_update_bits(dev, BQ2589X_REG_0D,
                               BQ2589X_VINDPM_MASK,
                               v << BQ2589X_VINDPM_SHIFT);
}
int bq2589x_set_input_current_limit(bq2589x_dev *dev, int curr_mA)
{
    uint8_t v = (curr_mA - BQ2589X_IINLIM_BASE) / BQ2589X_IINLIM_LSB;
    return bq2589x_update_bits(dev, BQ2589X_REG_00,
                               BQ2589X_IINLIM_MASK,
                               v << BQ2589X_IINLIM_SHIFT);
}
int bq2589x_set_vindpm_offset(bq2589x_dev *dev, int offset_mV)
{
    uint8_t v = (offset_mV - BQ2589X_VINDPMOS_BASE) / BQ2589X_VINDPMOS_LSB;
    return bq2589x_update_bits(dev, BQ2589X_REG_01,
                               BQ2589X_VINDPMOS_MASK,
                               v << BQ2589X_VINDPMOS_SHIFT);
}

/* ----------------------------------------------------------------- */
int bq2589x_get_charging_status(bq2589x_dev *dev)
{
    uint8_t v;
    if (bq2589x_read_reg(dev, BQ2589X_REG_0B, &v))
        return 4;                 /* error */
    return (v & BQ2589X_CHRG_STAT_MASK) >> BQ2589X_CHRG_STAT_SHIFT;
}

/* ----------------------------------------------------------------- */
int bq2589x_set_watchdog_timer(bq2589x_dev *dev, uint8_t timeout_s)
{
    uint8_t v = ((timeout_s - BQ2589X_WDT_BASE) / BQ2589X_WDT_LSB)
                << BQ2589X_WDT_SHIFT;
    return bq2589x_update_bits(dev, BQ2589X_REG_07,
                               BQ2589X_WDT_MASK, v);
}
int bq2589x_disable_watchdog_timer(bq2589x_dev *dev)
{
    return bq2589x_update_bits(dev, BQ2589X_REG_07,
                               BQ2589X_WDT_MASK,
                               BQ2589X_WDT_DISABLE << BQ2589X_WDT_SHIFT);
}
int bq2589x_reset_watchdog_timer(bq2589x_dev *dev)
{
    return bq2589x_update_bits(dev, BQ2589X_REG_03,
                               BQ2589X_WDT_RESET_MASK,
                               BQ2589X_WDT_RESET << BQ2589X_WDT_RESET_SHIFT);
}

/* ----------------------------------------------------------------- */
int bq2589x_force_dpdm(bq2589x_dev *dev)
{
    int ret = bq2589x_update_bits(dev, BQ2589X_REG_02,
                                  BQ2589X_FORCE_DPDM_MASK,
                                  BQ2589X_FORCE_DPDM << BQ2589X_FORCE_DPDM_SHIFT);
    if (ret) return ret;
    usleep(20000);               /* 20 ms – typical DPDM detection time */
    return BQ2589X_OK;
}
int bq2589x_reset_chip(bq2589x_dev *dev)
{
    return bq2589x_update_bits(dev, BQ2589X_REG_14,
                               BQ2589X_RESET_MASK,
                               BQ2589X_RESET << BQ2589X_RESET_SHIFT);
}
int bq2589x_enter_ship_mode(bq2589x_dev *dev)
{
    return bq2589x_update_bits(dev, BQ2589X_REG_09,
                               BQ2589X_BATFET_DIS_MASK,
                               BQ2589X_BATFET_OFF << BQ2589X_BATFET_DIS_SHIFT);
}
int bq2589x_enter_hiz_mode(bq2589x_dev *dev)
{
    return bq2589x_update_bits(dev, BQ2589X_REG_00,
                               BQ2589X_ENHIZ_MASK,
                               BQ2589X_HIZ_ENABLE << BQ2589X_ENHIZ_SHIFT);
}
int bq2589x_exit_hiz_mode(bq2589x_dev *dev)
{
    return bq2589x_update_bits(dev, BQ2589X_REG_00,
                               BQ2589X_ENHIZ_MASK,
                               BQ2589X_HIZ_DISABLE << BQ2589X_ENHIZ_SHIFT);
}
int bq2589x_get_hiz_mode(bq2589x_dev *dev, uint8_t *state)
{
    uint8_t v;
    int ret = bq2589x_read_reg(dev, BQ2589X_REG_00, &v);
    if (ret) return ret;
    *state = (v & BQ2589X_ENHIZ_MASK) >> BQ2589X_ENHIZ_SHIFT;
    return BQ2589X_OK;
}

/* ----------------------------------------------------------------- */
int bq2589x_pumpx_enable(bq2589x_dev *dev, int enable)
{
    uint8_t v = enable ? BQ2589X_PUMPX_ENABLE : BQ2589X_PUMPX_DISABLE;
    v <<= BQ2589X_EN_PUMPX_SHIFT;
    return bq2589x_update_bits(dev, BQ2589X_REG_04,
                               BQ2589X_EN_PUMPX_MASK, v);
}
int bq2589x_pumpx_increase_volt(bq2589x_dev *dev)
{
    return bq2589x_update_bits(dev, BQ2589X_REG_09,
                               BQ2589X_PUMPX_UP_MASK,
                               BQ2589X_PUMPX_UP << BQ2589X_PUMPX_UP_SHIFT);
}
int bq2589x_pumpx_increase_volt_done(bq2589x_dev *dev)
{
    uint8_t v;
    if (bq2589x_read_reg(dev, BQ2589X_REG_09, &v)) return BQ2589X_ERR;
    return (v & BQ2589X_PUMPX_UP_MASK) ? BQ2589X_ERR : BQ2589X_OK;
}
int bq2589x_pumpx_decrease_volt(bq2589x_dev *dev)
{
    return bq2589x_update_bits(dev, BQ2589X_REG_09,
                               BQ2589X_PUMPX_DOWN_MASK,
                               BQ2589X_PUMPX_DOWN << BQ2589X_PUMPX_DOWN_SHIFT);
}
int bq2589x_pumpx_decrease_volt_done(bq2589x_dev *dev)
{
    uint8_t v;
    if (bq2589x_read_reg(dev, BQ2589X_REG_09, &v)) return BQ2589X_ERR;
    return (v & BQ2589X_PUMPX_DOWN_MASK) ? BQ2589X_ERR : BQ2589X_OK;
}

/* ----------------------------------------------------------------- */
int bq2589x_force_ico(bq2589x_dev *dev)
{
    return bq2589x_update_bits(dev, BQ2589X_REG_09,
                               BQ2589X_FORCE_ICO_MASK,
                               BQ2589X_FORCE_ICO << BQ2589X_FORCE_ICO_SHIFT);
}
int bq2589x_check_force_ico_done(bq2589x_dev *dev)
{
    uint8_t v;
    if (bq2589x_read_reg(dev, BQ2589X_REG_14, &v)) return BQ2589X_ERR;
    return (v & BQ2589X_ICO_OPTIMIZED_MASK) ? BQ2589X_ERR : BQ2589X_OK;
}

/* ----------------------------------------------------------------- */
int bq2589x_enable_term(bq2589x_dev *dev, bool enable)
{
    uint8_t v = enable ? BQ2589X_TERM_ENABLE : BQ2589X_TERM_DISABLE;
    v <<= BQ2589X_EN_TERM_SHIFT;
    return bq2589x_update_bits(dev, BQ2589X_REG_07,
                               BQ2589X_EN_TERM_MASK, v);
}
int bq2589x_enable_auto_dpdm(bq2589x_dev *dev, bool enable)
{
    uint8_t v = enable ? BQ2589X_AUTO_DPDM_ENABLE : BQ2589X_AUTO_DPDM_DISABLE;
    v <<= BQ2589X_AUTO_DPDM_EN_SHIFT;
    return bq2589x_update_bits(dev, BQ2589X_REG_02,
                               BQ2589X_AUTO_DPDM_EN_MASK, v);
}
int bq2589x_use_absolute_vindpm(bq2589x_dev *dev, bool enable)
{
    uint8_t v = enable ? BQ2589X_FORCE_VINDPM_ENABLE : BQ2589X_FORCE_VINDPM_DISABLE;
    v <<= BQ2589X_FORCE_VINDPM_SHIFT;
    return bq2589x_update_bits(dev, BQ2589X_REG_0D,
                               BQ2589X_FORCE_VINDPM_MASK, v);
}
int bq2589x_enable_ico(bq2589x_dev *dev, bool enable)
{
    uint8_t v = enable ? BQ2589X_ICO_ENABLE : BQ2589X_ICO_DISABLE;
    v <<= BQ2589X_ICOEN_SHIFT;
    return bq2589x_update_bits(dev, BQ2589X_REG_02,
                               BQ2589X_ICOEN_MASK, v);
}

/* ----------------------------------------------------------------- */
int bq2589x_read_idpm_limit(bq2589x_dev *dev)
{
    uint8_t v;
    if (bq2589x_read_reg(dev, BQ2589X_REG_13, &v)) return BQ2589X_ERR;
    return BQ2589X_IDPM_LIM_BASE +
           (((v & BQ2589X_IDPM_LIM_MASK) >> BQ2589X_IDPM_LIM_SHIFT)
            * BQ2589X_IDPM_LIM_LSB);
}

/* ----------------------------------------------------------------- */
bool bq2589x_is_charge_done(bq2589x_dev *dev)
{
    uint8_t v;
    if (bq2589x_read_reg(dev, BQ2589X_REG_0B, &v))
        return false;
    return ((v & BQ2589X_CHRG_STAT_MASK) >> BQ2589X_CHRG_STAT_SHIFT)
           == BQ2589X_CHRG_STAT_CHGDONE;
}

/* ----------------------------------------------------------------- */
int bq2589x_init_device(bq2589x_dev *dev)
{
    bq2589x_disable_watchdog_timer(dev);
    return bq2589x_set_charge_current(dev, 2560);   /* 2.56 A example */
}

/* ----------------------------------------------------------------- */
int bq2589x_detect_device(bq2589x_dev *dev,
                          bq2589x_part_no *part_no, int *revision)
{
    uint8_t v;
    if (bq2589x_read_reg(dev, BQ2589X_REG_14, &v))
        return BQ2589X_ERR;
    *part_no  = (bq2589x_part_no)((v & BQ2589X_PN_MASK) >> BQ2589X_PN_SHIFT);
    *revision = (v & BQ2589X_DEV_REV_MASK) >> BQ2589X_DEV_REV_SHIFT;
    return BQ2589X_OK;
}

/* ----------------------------------------------------------------- */
int bq2589x_enable_max_charge(bq2589x_dev *dev, bool enable)
{
    uint8_t hv = enable ? BQ2589X_HVDCP_ENABLE : BQ2589X_HVDCP_DISABLE;
    uint8_t mc = enable ? BQ2589X_MAXC_ENABLE  : BQ2589X_MAXC_DISABLE;
    hv <<= BQ2589X_HVDCPEN_SHIFT;
    mc <<= BQ2589X_MAXCEN_SHIFT;

    int ret = bq2589x_update_bits(dev, BQ2589X_REG_02,
                                  BQ2589X_HVDCPEN_MASK, hv);
    if (ret) return ret;
    return bq2589x_update_bits(dev, BQ2589X_REG_02,
                               BQ2589X_MAXCEN_MASK, mc);
}