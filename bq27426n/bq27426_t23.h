#ifndef BQ27426_T23_H
#define BQ27426_T23_H

#include <stdint.h>
#include <stddef.h>

/* Default bus/address (override in open) */
#define BQ27426_DEFAULT_DEV   "/dev/i2c-2"
#define BQ27426_DEFAULT_ADDR  0x55

/* Standard command registers (16-bit words, LE) */
#define BQ27426_CMD_CNTL           0x00
#define BQ27426_CMD_TEMP           0x02  /* 0.1K */
#define BQ27426_CMD_VOLT           0x04  /* mV */
#define BQ27426_CMD_FLAGS          0x06
#define BQ27426_CMD_NOM_CAP        0x08  /* mAh */
#define BQ27426_CMD_AVAIL_CAP      0x0A  /* mAh */
#define BQ27426_CMD_REM_CAP        0x0C  /* mAh */
#define BQ27426_CMD_FULL_CAP       0x0E  /* mAh */
#define BQ27426_CMD_AVG_CURR       0x10  /* mA (signed) */
#define BQ27426_CMD_AVG_PWR        0x18  /* mW (signed) */
#define BQ27426_CMD_SOC            0x1C  /* % (low byte) */
#define BQ27426_CMD_INT_TEMP       0x1E  /* 0.1K */
#define BQ27426_CMD_SOH            0x20  /* % (low byte), status (high) */

/* Extended data (data memory) */
#define BQ27426_EXT_CONTROL        0x61  /* BlockDataControl() */
#define BQ27426_EXT_DATACLASS      0x3E  /* DataClass() */
#define BQ27426_EXT_DATABLOCK      0x3F  /* DataBlock() (block index) */
#define BQ27426_EXT_BLOCKDATA      0x40  /* 32-byte window start */
#define BQ27426_EXT_CHECKSUM       0x60  /* BlockDataChecksum() */

/* Control() subcommands */
#define CNTL_STATUS                0x0000
#define CNTL_CONTROL_STATUS    0x0000
#define CNTL_DEVICE_TYPE           0x0001
#define CNTL_FW_VERSION            0x0002
#define CNTL_UNSEAL_KEY          0x8000        // write twice       // CONTROL_STATUS subcommand
#define CNTL_SET_CFGUPDATE         0x0013
#define CNTL_SOFT_RESET            0x0042
#define CNTL_EXIT_CFGUPDATE        0x0043
#define CNTL_SEAL                  0x0020

/* Classes (subclass IDs) you touched in the ESP code */
#define CLASS_STATE                0x52   /* “State” subclass */

/* Offsets inside CLASS_STATE (verify against your datasheet) */

#define OFFS_DESIGN_CAP_mAh        0x06   /* uint16 LE */
#define OFFS_DESIGN_EN_mWh         0x08   /* uint16 LE */
#define OFFS_TERMINATE_VOLT_mV     0x0A   /* uint16 LE */
#define OFFS_TAPER_RATE_0p1h       0x15   /* uint16 LE (decimal 21) */


/* STATE class offsets (you already use these) */
#define OFFS_QMAX_mAh                 0    /* 2 bytes, big-endian */
#define OFFS_UPDATE_STATUS            0x02    /* 1 byte: bit0/1 for learning */

/* If you already have this under another name, keep yours */
#ifndef CLASS_R_A_RAM
#define CLASS_R_A_RAM                 0x58 /* Ra RAM subclass ID for BQ27426 */
#endif

/* Flags bits (common ones) */
#define FLAG_DSG   (1u << 0)
#define FLAG_FC    (1u << 9)
#define FLAG_CFGUP (1u << 4)
#define FLAG_VOK   (1u << 2)


#define CNTL_CHEM_ID          0x0008
#define CLASS_CHEM_ID         82      // subclass ID for ChemID
#define DEFAULT_CHEMID        0x1202  // LiCoO₂ 4.2 V cell (typical)

typedef struct {
    int fd;
    int addr;
    char devpath[64];
} bq27426_t;

/* Open/close */
int  bq27426_open(bq27426_t *ctx, const char *devpath, int addr);
void bq27426_close(bq27426_t *ctx);

/* Low-level I2C helpers */
int  bq_rd8 (bq27426_t *ctx, uint8_t reg, uint8_t *val);
int  bq_wr8 (bq27426_t *ctx, uint8_t reg, uint8_t val);
int  bq_rd16(bq27426_t *ctx, uint8_t reg, uint16_t *val_le);
int  bq_wr16le(bq27426_t *ctx, uint8_t reg, uint16_t val_le);

/* Control subcommands */
int  bq_control(bq27426_t *ctx, uint16_t subcmd);
int  bq_unseal_try(bq27426_t *ctx);
int  bq_enter_cfg(bq27426_t *ctx);
int  bq_exit_cfg(bq27426_t *ctx, int resim);

/* Extended data block access (32 bytes window) */
int  bq_select_class_block(bq27426_t *ctx, uint8_t class_id, uint8_t block_idx);
int  bq_block_read(bq27426_t *ctx, uint8_t *blk32);
int  bq_block_write_with_checksum(bq27426_t *ctx, const uint8_t *blk32);

/* High-level “write few bytes in class/offset” helper */
int  bq_write_extended(bq27426_t *ctx, uint8_t class_id, uint16_t offset, const uint8_t *data, size_t len);
int  bq_read_extended (bq27426_t *ctx, uint8_t class_id, uint16_t offset, uint8_t *data, size_t len);

/* Setters */
int  bq_set_design_capacity(bq27426_t *ctx, uint16_t mAh);
int  bq_set_design_energy  (bq27426_t *ctx, uint16_t mWh);
int  bq_set_terminate_voltage(bq27426_t *ctx, uint16_t mV);   /* clamp 2500..3700 like your code */
int  bq_set_taper_rate     (bq27426_t *ctx, uint16_t rate0p1h);


int bq_verify_state_params_verbose(bq27426_t *ctx,
                                   uint16_t expect_cap_mAh,
                                   uint16_t expect_en_mWh,
                                   uint16_t expect_tv_mV,
                                   uint16_t expect_taper_01h);


/* Getters */
int  bq_voltage_mV (bq27426_t *ctx, uint16_t *mV);
int  bq_current_mA (bq27426_t *ctx, int16_t *mA);
int  bq_power_mW   (bq27426_t *ctx, int16_t *mW);
int  bq_soc_pct    (bq27426_t *ctx, uint8_t *pct);
int  bq_soh_pct    (bq27426_t *ctx, uint8_t *pct);
int  bq_temp_cell_c01K(bq27426_t *ctx, uint16_t *t01K);   /* raw 0.1K */
double bq_k01_to_c(double k01);                           /* helper */

int  bq_capacity_remain_mAh (bq27426_t *ctx, uint16_t *mAh);
int  bq_capacity_full_mAh   (bq27426_t *ctx, uint16_t *mAh);
int  bq_capacity_nom_mAh    (bq27426_t *ctx, uint16_t *mAh);
int  bq_capacity_avail_mAh  (bq27426_t *ctx, uint16_t *mAh);



int bq_qmax_read(bq27426_t *ctx, uint16_t *qmax_mAh);
int bq_qmax_write(bq27426_t *ctx, uint16_t qmax_mAh);        /* optional */
int bq_ra_table_read(bq27426_t *ctx, uint16_t ra[15]);
void bq_print_qmax_and_ra(bq27426_t *ctx);
int bq_set_learning_mode(bq27426_t *ctx, int enable);        /* 1=on, 0=off */


int bq_dump_control_status(bq27426_t *ctx);
int bq_dump_flags(bq27426_t *ctx);


int bq_learning_monitor(bq27426_t *ctx, unsigned period_ms, unsigned max_minutes);



int bq_get_chem_id(bq27426_t *ctx, uint16_t *chem_id);
int bq_set_chem_id(bq27426_t *ctx, uint16_t chem_id);

int bq_set_chem_1202(bq27426_t *ctx);

#endif /* BQ27426_T23_H */
