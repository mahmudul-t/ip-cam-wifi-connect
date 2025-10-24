#include "bq27426_t23.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

static uint8_t checksum32(const uint8_t *blk32) 
{
    unsigned sum = 0;
    for (int i = 0; i < 32; i++) sum += blk32[i];
    return (uint8_t)(0xFF - (sum & 0xFF));
}

/* --------- Open/close ---------- */
int bq27426_open(bq27426_t *ctx, const char *devpath, int addr) 
{
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(*ctx));
    ctx->addr = (addr > 0) ? addr : BQ27426_DEFAULT_ADDR;
    snprintf(ctx->devpath, sizeof(ctx->devpath), "%s", devpath ? devpath : BQ27426_DEFAULT_DEV);

    ctx->fd = open(ctx->devpath, O_RDWR);
    if (ctx->fd < 0) return -1;
    if (ioctl(ctx->fd, I2C_SLAVE, ctx->addr) < 0) 
    {
        close(ctx->fd);
        ctx->fd = -1;
        return -1;
    }
    return 0;
}
void bq27426_close(bq27426_t *ctx) 
{
    if (ctx && ctx->fd >= 0) { close(ctx->fd); ctx->fd = -1; }
}

/* --------- I2C helpers ---------- */
int bq_rd8(bq27426_t *ctx, uint8_t reg, uint8_t *val) 
{
    if (write(ctx->fd, &reg, 1) != 1) return -1;
    if (read (ctx->fd, val, 1) != 1)  return -1;
    return 0;
}
int bq_wr8(bq27426_t *ctx, uint8_t reg, uint8_t val) 
{
    uint8_t b[2] = {reg, val};
    if (write(ctx->fd, b, 2) != 2) return -1;
    return 0;
}
int bq_rd16(bq27426_t *ctx, uint8_t reg, uint16_t *val_le) 
{
    uint8_t b[2];
    if (write(ctx->fd, &reg, 1) != 1) return -1;
    if (read (ctx->fd, b, 2) != 2)  return -1;
    *val_le = (uint16_t)(b[0] | (b[1] << 8));
    return 0;
}
int bq_wr16le(bq27426_t *ctx, uint8_t reg, uint16_t val_le) 
{
    uint8_t b[3] = {reg, (uint8_t)(val_le & 0xFF), (uint8_t)(val_le >> 8)};
    if (write(ctx->fd, b, 3) != 3) return -1;
    return 0;
}

/* --------- Control subcommands ---------- */
int bq_control(bq27426_t *ctx, uint16_t subcmd) 
{
    if (bq_wr16le(ctx, BQ27426_CMD_CNTL, subcmd) < 0) 
    {
        printf("error in bq_control\n");
        return -1;
    }
    return 0;
}



// int bq_unseal_try(bq27426_t *ctx) 
// {
//     /* Try default unseal keys; ignore errors */
//     (void)bq_control(ctx, CNTL_UNSEAL_KEY_1);
//     usleep(20000);
//     (void)bq_control(ctx, CNTL_UNSEAL_KEY_2);
//     usleep(20000);


//     // Verify SS bit cleared
//     uint16_t status;
//     if (bq_control(ctx, CNTL_STATUS) < 0) return -1;
//     if (bq_rd16(ctx, BQ27426_CMD_CNTL, &status) < 0) return -1;
//     // SS bit is 13 in CONTROL_STATUS
//     return (status & (1u<<13)) ? -1 : 0;
// }


int bq_unseal_try(bq27426_t *ctx)
{
    if (bq_control(ctx, CNTL_UNSEAL_KEY) < 0) 
    {
        printf("[BQ] ERROR: first key write failed");
        return -1;
    }
    
    usleep(20000);

    if (bq_control(ctx, CNTL_UNSEAL_KEY) < 0) 
    {
        printf("[BQ] ERROR: second key write failed");
        return -1;
    }
    usleep(20000);

   
    for (int i = 0; i < 50; i++) 
    {
        uint16_t status;
        if (bq_control(ctx, CNTL_CONTROL_STATUS) < 0) 
        {
            printf("ERROR: control_status subcmd write failed\n");
            // return -1;
        }

        if (bq_rd16(ctx, BQ27426_CMD_CNTL, &status) < 0) 
        {
            printf("ERROR: failed to read CONTROL_STATUS\n");
            // return -1;
        }

        if ((status & (1u << 13)) == 0) 
        {
            printf("[BQ] -> Device successfully unsealed!\n");
            return 0;
        }
        usleep(20000);
    }

    printf("ERROR: Unseal timeout. Still sealed after 1 second.\n");
    errno = EACCES;
    return -1;
}

int bq_enter_cfg(bq27426_t *ctx) 
{
    if (bq_control(ctx, CNTL_SET_CFGUPDATE) < 0) return -1;
    /* poll CFGUPMODE bit via FLAGS if needed; simple delay is often enough */
    usleep(30000);
    return 0;
}


int bq_exit_cfg(bq27426_t *ctx, int resim) 
{
    if (resim) 
    {
        if (bq_control(ctx, CNTL_SOFT_RESET) < 0) return -1;
    } 
    else 
    {
        if (bq_control(ctx, CNTL_EXIT_CFGUPDATE) < 0) return -1;
    }
    usleep(30000);
    return 0;
}




int bq_make_sealed(bq27426_t *ctx) 
{

    bq_control(ctx, CNTL_SEAL);
   
    return 0;
}

/* --------- Block access ---------- */
int bq_select_class_block(bq27426_t *ctx, uint8_t class_id, uint8_t block_idx) {
    if (bq_wr8(ctx, BQ27426_EXT_CONTROL,  0x00) < 0) 
    {
        printf("error control\n");
        return -1;      /* enable */
    }
    if (bq_wr8(ctx, BQ27426_EXT_DATACLASS, class_id) < 0) 
    {
        printf("erorr data class\n");
        return -1; /* class */
    }
    if (bq_wr8(ctx, BQ27426_EXT_DATABLOCK, block_idx) < 0) 
    {
        printf("error data block\n");
        return -1;/* block */
    }
    usleep(10000);
    return 0;
}
int bq_block_read(bq27426_t *ctx, uint8_t *blk32) 
{
    if (!blk32) return -1;
    uint8_t reg = BQ27426_EXT_BLOCKDATA;
    if (write(ctx->fd, &reg, 1) != 1) return -1;
    if (read (ctx->fd, blk32, 32) != 32) return -1;
    return 0;
}
int bq_block_write_with_checksum(bq27426_t *ctx, const uint8_t *blk32) 
{
    if (!blk32) return -1;
    /* Write all 32 bytes */
    for (int i = 0; i < 32; i++) 
    {
        if (bq_wr8(ctx, (uint8_t)(BQ27426_EXT_BLOCKDATA + i), blk32[i]) < 0) return -1;
    }
    /* Then checksum */
    uint8_t csum = checksum32(blk32);
    if (bq_wr8(ctx, BQ27426_EXT_CHECKSUM, csum) < 0) return -1;
    usleep(10000);
    return 0;
}

static int bq_wait_ready(bq27426_t *ctx, int max_ms)
{
    uint16_t f;
    for (int i = 0; i < max_ms/50; i++) 
    {
        if (bq_rd16(ctx, BQ27426_CMD_FLAGS, &f) == 0) 
        {
            if ( (f & FLAG_CFGUP) == 0) 
            {
                printf("CFGUPMODE bit is now: %d\n ", (f & FLAG_CFGUP));
                return 0;
            }
        }
        usleep(50*1000);
    }
    return -1;
}


int bq_write_extended(bq27426_t *ctx, uint8_t class_id, uint16_t offset, const uint8_t *data, size_t len) 
{
    if (!data || !len) return -1;

    /* Enter cfg */
    // if (bq_unseal_try(ctx), bq_enter_cfg(ctx) < 0) return -1;
 if (bq_unseal_try(ctx) < 0) 
    {
        printf("error :unseal\n");
        return -1;
    }   

    if (bq_enter_cfg(ctx)   < 0) 
    {
        printf("error: enter_cfg");
        return -1;
    }

    size_t done = 0;
    while (done < len) 
    {
        uint8_t block_idx = (offset / 32);
        uint8_t inblock   = (uint8_t)(offset % 32);
        size_t  chunk     = (len - done);
        if (chunk > (32 - inblock)) chunk = 32 - inblock;

        uint8_t blk[32];
        if (bq_select_class_block(ctx, class_id, block_idx) < 0) goto err;
        if (bq_block_read(ctx, blk) < 0) goto err;

        memcpy(&blk[inblock], &data[done], chunk);

        if (bq_block_write_with_checksum(ctx, blk) < 0) goto err;

        offset += (uint16_t)chunk;
        done   += chunk;
    }

    bq_exit_cfg(ctx, /*resim*/1);
    bq_wait_ready(ctx, 5000);
    return 0;
err:
    bq_exit_cfg(ctx, /*resim*/1);
    bq_wait_ready(ctx, 5000);
    return -1;
}

/* Read len bytes from class/offset (may span blocks) */
int bq_read_extended(bq27426_t *ctx, uint8_t class_id, uint16_t offset, uint8_t *data, size_t len) 
{
    if (!data || !len) return -1;

    if (bq_unseal_try(ctx) < 0) 
    {
        printf("error :unseal\n");
        return -1;
    }   

    if (bq_enter_cfg(ctx)   < 0) 
    {
        printf("error: enter_cfg");
        return -1;
    }

    size_t done = 0;
    while (done < len) 
    {
        uint8_t block_idx = (offset / 32);
        uint8_t inblock   = (uint8_t)(offset % 32);
        size_t  chunk     = (len - done);
        if (chunk > (32 - inblock)) chunk = 32 - inblock;

        uint8_t blk[32];
        if (bq_select_class_block(ctx, class_id, block_idx) < 0) goto err;
        if (bq_block_read(ctx, blk) < 0) goto err;

        memcpy(&data[done], &blk[inblock], chunk);

        offset += (uint16_t)chunk;
        done   += chunk;
    }

    bq_exit_cfg(ctx, /*resim*/0);
    bq_wait_ready(ctx, 5000);
    return 0;
err:
    bq_exit_cfg(ctx, /*resim*/0);
    bq_wait_ready(ctx, 5000);
    return -1;
}

/* --------- Setters (STATE class) ---------- */
int bq_set_design_capacity(bq27426_t *ctx, uint16_t mAh) 
{
    /* MSB first for Data Memory */
    uint8_t be[2] = { (uint8_t)(mAh >> 8), (uint8_t)(mAh & 0xFF) };
    return bq_write_extended(ctx, CLASS_STATE, OFFS_DESIGN_CAP_mAh, be, 2);
}

int bq_set_design_energy(bq27426_t *ctx, uint16_t mWh) 
{
    uint8_t be[2] = { (uint8_t)(mWh >> 8), (uint8_t)(mWh & 0xFF) };
    return bq_write_extended(ctx, CLASS_STATE, OFFS_DESIGN_EN_mWh, be, 2);
}

int bq_set_terminate_voltage(bq27426_t *ctx, uint16_t mV) 
{
    if (mV < 2500) mV = 2500;
    if (mV > 3700) mV = 3700;
    uint8_t be[2] = { (uint8_t)(mV >> 8), (uint8_t)(mV & 0xFF) };
    return bq_write_extended(ctx, CLASS_STATE, OFFS_TERMINATE_VOLT_mV, be, 2);
}

int bq_set_taper_rate(bq27426_t *ctx, uint16_t rate0p1h) 
{
    if (rate0p1h > 2000) rate0p1h = 2000;
    uint8_t be[2] = { (uint8_t)(rate0p1h >> 8), (uint8_t)(rate0p1h & 0xFF) };
    return bq_write_extended(ctx, CLASS_STATE, OFFS_TAPER_RATE_0p1h, be, 2);
}


/* --------- Verify design parameters --------- */
int bq_verify_state_params_verbose(bq27426_t *ctx,
                                   uint16_t expect_cap_mAh,
                                   uint16_t expect_en_mWh,
                                   uint16_t expect_tv_mV,
                                   uint16_t expect_taper_01h)
{
    uint8_t b[2];
    uint16_t cap=0,en=0,tv=0,taper=0;
    int rc=0;

    printf("\n--- Verifying BQ27426 Design Parameters [test] ---\n");

    if ((rc = bq_read_extended(ctx, CLASS_STATE, OFFS_DESIGN_CAP_mAh, b, 2)) == 0) {
        cap = (b[0]<<8) | b[1];
        printf("Design Capacity     : %u mAh", cap);
        if (expect_cap_mAh) printf("  (expected %u) %s", expect_cap_mAh, cap==expect_cap_mAh?"OK":"MISMATCH");
        printf("\n");
    } 
    else 
    { 
        printf("Read Design Capacity failed"); return -1; 
    }

    if ((rc = bq_read_extended(ctx, CLASS_STATE, OFFS_DESIGN_EN_mWh, b, 2)) == 0) 
    {
        en = (b[0]<<8) | b[1];
        printf("Design Energy       : %u mWh", en);
        if (expect_en_mWh) printf("  (expected %u) %s", expect_en_mWh, en==expect_en_mWh?"OK":"MISMATCH");
        printf("\n");
    } 
    else 
    { 
        printf("Error: Read Design Energy"); 
        return -1; 
    }

    if ((rc = bq_read_extended(ctx, CLASS_STATE, OFFS_TERMINATE_VOLT_mV, b, 2)) == 0) 
    {
        tv = (b[0]<<8) | b[1];
        printf("Terminate Voltage   : %u mV", tv);
        if (expect_tv_mV) printf("  (expected %u) %s", expect_tv_mV, tv==expect_tv_mV?"OK":"MISMATCH");
        printf("\n");
    } else { printf("Read Terminate Voltage"); return -1; }

    if ((rc = bq_read_extended(ctx, CLASS_STATE, OFFS_TAPER_RATE_0p1h, b, 2)) == 0) {
        taper = (b[0]<<8) | b[1];
        printf("Taper Rate          : %u (0.1h units)", taper);
        if (expect_taper_01h) printf("  (expected %u) %s", expect_taper_01h, taper==expect_taper_01h?"OK":"MISMATCH");
        printf("\n");
    } else { printf("Read Taper Rate"); return -1; }

    printf("------------------------------------------\n\n");
    return 0;
}






/* --------- Getters ---------- */
int bq_voltage_mV(bq27426_t *ctx, uint16_t *mV) {
    return bq_rd16(ctx, BQ27426_CMD_VOLT, mV);
}
int bq_current_mA(bq27426_t *ctx, int16_t *mA) {
    uint16_t w;
    if (bq_rd16(ctx, BQ27426_CMD_AVG_CURR, &w) < 0) return -1;
    *mA = (int16_t)w;
    return 0;
}
int bq_power_mW(bq27426_t *ctx, int16_t *mW) {
    uint16_t w;
    if (bq_rd16(ctx, BQ27426_CMD_AVG_PWR, &w) < 0) return -1;
    *mW = (int16_t)w;
    return 0;
}
int bq_soc_pct(bq27426_t *ctx, uint8_t *pct) {
    uint16_t w;
    if (bq_rd16(ctx, BQ27426_CMD_SOC, &w) < 0) return -1;
    *pct = (uint8_t)(w & 0xFF);
    return 0;
}
int bq_soh_pct(bq27426_t *ctx, uint8_t *pct) {
    uint16_t w;
    if (bq_rd16(ctx, BQ27426_CMD_SOH, &w) < 0) return -1;
    *pct = (uint8_t)(w & 0xFF);
    return 0;
}
int bq_temp_cell_c01K(bq27426_t *ctx, uint16_t *t01K) {
    return bq_rd16(ctx, BQ27426_CMD_TEMP, t01K);
}
double bq_k01_to_c(double k01) {
    return (k01 - 2731.0) / 10.0;
}

int bq_capacity_remain_mAh(bq27426_t *ctx, uint16_t *mAh) { return bq_rd16(ctx, BQ27426_CMD_REM_CAP,  mAh); }
int bq_capacity_full_mAh  (bq27426_t *ctx, uint16_t *mAh) { return bq_rd16(ctx, BQ27426_CMD_FULL_CAP, mAh); }
int bq_capacity_nom_mAh   (bq27426_t *ctx, uint16_t *mAh) { return bq_rd16(ctx, BQ27426_CMD_NOM_CAP,  mAh); }
int bq_capacity_avail_mAh (bq27426_t *ctx, uint16_t *mAh) { return bq_rd16(ctx, BQ27426_CMD_AVAIL_CAP,mAh); }

int bq_device_type(bq27426_t *ctx, uint16_t *devtype) 
{
    if (bq_control(ctx, CNTL_DEVICE_TYPE) < 0) return -1;
    return bq_rd16(ctx, BQ27426_CMD_CNTL, devtype);
}


/* ---- Qmax helpers ---- */
int bq_qmax_read(bq27426_t *ctx, uint16_t *qmax_mAh)
{
    if (!qmax_mAh) return -1;
    uint8_t b[2];
    if (bq_read_extended(ctx, CLASS_STATE, OFFS_QMAX_mAh, b, 2) < 0) return -1;
    *qmax_mAh = (uint16_t)((b[0] << 8) | b[1]);  /* big-endian in data memory */
    return 0;
}

/* Optional: write Qmax (rarely needed; let IT learn it). */
int bq_qmax_write(bq27426_t *ctx, uint16_t qmax_mAh)
{
    uint8_t be[2] = { (uint8_t)(qmax_mAh >> 8), (uint8_t)(qmax_mAh & 0xFF) };
    return bq_write_extended(ctx, CLASS_STATE, OFFS_QMAX_mAh, be, 2);
}

/* ---- Ra table helpers ---- */
/* Reads 15 16-bit entries from Ra RAM subclass (30 bytes). */
int bq_ra_table_read(bq27426_t *ctx, uint16_t ra[15])
{
    if (!ra) return -1;

    /* Ra table spans 30 bytes starting at offset 0 in CLASS_R_A_RAM. */
    uint8_t raw[30];
    if (bq_read_extended(ctx, CLASS_R_A_RAM, 0, raw, sizeof(raw)) < 0)
        return -1;

    for (int i = 0; i < 15; i++) {
        /* data memory stores big-endian for these words */
        ra[i] = (uint16_t)((raw[i*2] << 8) | raw[i*2 + 1]);
    }
    return 0;
}



/* 
 * Control learning / production state via UpdateStatus.
 *
 * - BQ_LEARN_ENABLE:
 *      set bits [1:0] = 0b11
 *      bit7 = 0
 *      -> learning mode ON (Qmax/Ra free to update)
 *
 * - BQ_LEARN_FREEZE_UNSEALED:
 *      clear bits [1:0]
 *      bit7 = 0
 *      -> learning mode OFF, gauge keeps current Qmax/Ra,
 *         won't freely update them anymore, stays UNSEALED on next reset
 *
 * - BQ_LEARN_FREEZE_SEALED:
 *      clear bits [1:0]
 *      set bit7 = 1
 *      -> lock in learned data, device will boot SEALED after reset
 *         (this is what you ship in production units)
 *
 * Returns 0 on success, -1 on error.
 */
int bq_set_learning_mode(bq27426_t *ctx, bq_learn_mode_t mode)
{
    uint8_t us; // UpdateStatus byte

    // Read current UpdateStatus from STATE subclass offset OFFS_UPDATE_STATUS
    if (bq_read_extended(ctx, CLASS_STATE, OFFS_UPDATE_STATUS, &us, 1) < 0) {
        printf("[BQ] bq_set_learning_mode: read UpdateStatus failed\n");
        return -1;
    }

    // Work on a local copy
    uint8_t new_us = us;

    // First deal with bits [1:0] (Qmax/Ra learn enable)
    switch (mode) {
    case BQ_LEARN_ENABLE:
        // set bits 0 and 1 => 0b11
        new_us |= 0x03;      // allow Qmax/Ra updates
        new_us &= ~(1u<<7);  // make sure bit7 = 0 (don’t force seal on exit)
        break;

    case BQ_LEARN_FREEZE_UNSEALED:
        // clear bits 0 and 1
        new_us &= ~0x03;
        // keep bit7 = 0 so the gauge does NOT auto-seal after reset
        new_us &= ~(1u<<7);
        break;

    case BQ_LEARN_FREEZE_SEALED:
        // clear bits 0 and 1
        new_us &= ~0x03;
        // set bit7 = 1 => after reset / exit config it’ll come up SEALED
        new_us |=  (1u<<7);
        break;

    default:
        printf("[BQ] bq_set_learning_mode: invalid mode %d\n", mode);
        return -1;
    }

    // If nothing changed, just report success
    if (new_us == us) {
        printf("[BQ] bq_set_learning_mode: no change (mode=%d)\n", mode);
        return 0;
    }

    // Write it back
    if (bq_write_extended(ctx, CLASS_STATE, OFFS_UPDATE_STATUS, &new_us, 1) < 0) {
        printf("[BQ] bq_set_learning_mode: write UpdateStatus failed\n");
        return -1;
    }

    printf("[BQ] bq_set_learning_mode: UpdateStatus 0x%02X -> 0x%02X (mode=%d)\n",
           us, new_us, mode);

    return 0;
}




// /* ---- Learning mode bit (STATE: offset 2, bit0/bit1) ---- */
// int bq_set_learning_mode(bq27426_t *ctx, int enable)
// {
//     /* UpdateStatus (offset 2): set bits [1:0] = 0b11 to allow Qmax/Ra updates. */
//     uint8_t v;
//     if (bq_read_extended(ctx, CLASS_STATE, OFFS_UPDATE_STATUS, &v, 1) < 0)
//         return -1;

//     if (enable) v = (v | 0x03);
//     else        v = (v & ~0x03);

//     return bq_write_extended(ctx, CLASS_STATE, OFFS_UPDATE_STATUS, &v, 1);
// }

/* ---- Pretty printer ---- */
void bq_print_qmax_and_ra(bq27426_t *ctx)
{
    uint16_t qmax = 0;
    uint16_t ra[15] = {0};

    if (bq_qmax_read(ctx, &qmax) == 0)
        printf("Qmax         : %u mAh\n", qmax);
    else
        printf("Qmax         : <read failed>\n");

    if (bq_ra_table_read(ctx, ra) == 0) {
        printf("Ra Table (mΩ codes):\n");
        for (int i = 0; i < 15; i++) {
            printf("  Ra[%02d] = %u\n", i, ra[i]);
        }
    } else {
        printf("Ra Table     : <read failed>\n");
    }
}





int bq_dump_control_status(bq27426_t *ctx)
{
    uint16_t val = 0;

    /* Issue CONTROL_STATUS subcommand (0x0000), then read CNTL (0x00) */
    if (/*bq_control(ctx, CNTL_STATUS) < 0 ||*/ bq_rd16(ctx, BQ27426_CMD_CNTL, &val) < 0) {
        printf("error: CONTROL_STATUS read failed\n");
        return -1;
    }

    printf("[BQ] CONTROL_STATUS = 0x%04X\n", val);
    printf("     [%d] SHUTDOWNEN\n", !!(val & CS_SHUTDOWNEN));
    printf("     [%d] WDRESET\n",     !!(val & CS_WDRESET));
    printf("     [%d] SS (sealed)\n", !!(val & CS_SS));
    printf("     [%d] CALMODE\n",     !!(val & CS_CALMODE));
    printf("     [%d] CCA (coulomb counter calib)\n", !!(val & CS_CCA));
    printf("     [%d] BCA (board calib)\n",          !!(val & CS_BCA));
    printf("     [%d] QMAX_UP\n",     !!(val & CS_QMAX_UP));
    printf("     [%d] RES_UP\n",      !!(val & CS_RES_UP));
    printf("     [%d] INITCOMP\n",    !!(val & CS_INITCOMP));
    printf("     [%d] SLEEP\n",       !!(val & CS_SLEEP));
    printf("     [%d] LDMD (const-power model)\n", !!(val & CS_LDMD));
    printf("     [%d] RUP_DIS (Ra updates disabled)\n", !!(val & CS_RUP_DIS));
    printf("     [%d] VOK (OCV valid for Qmax updates)\n", !!(val & CS_VOK));
    printf("     [%d] CHEMCHANGE\n",  !!(val & CS_CHEMCHANGE));
    printf("-------------------------------------------\n");
    return 0;
}

/* FLAGS (Command 0x06) — runtime status */
int bq_dump_flags(bq27426_t *ctx)
{
    uint16_t f;
    if (bq_rd16(ctx, BQ27426_CMD_FLAGS, &f) < 0) {
        printf("error: FLAGS read failed\n");
        return -1;
    }

    printf("[BQ] FLAGS = 0x%04X\n", f);
    printf("     [%d] OT (over-temp)\n",        !!(f & FLAG_OT));
    printf("     [%d] UT (under-temp)\n",       !!(f & FLAG_UT));
    printf("     [%d] FC (full charge)\n",      !!(f & FLAG_FC));
    printf("     [%d] CHG (charging)\n",        !!(f & FLAG_CHG));
    printf("     [%d] OCVTAKEN\n",              !!(f & FLAG_OCVTAKEN));
    printf("     [%d] DOD_CORRECT\n",           !!(f & FLAG_DODCORRECT));
    printf("     [%d] ITPOR (reset occurred)\n",!!(f & FLAG_ITPOR));
    printf("     [%d] CFGUPMODE\n",             !!(f & FLAG_CFGUP));
    printf("     [%d] BAT_DET\n",               !!(f & FLAG_BAT_DET));
    printf("     [%d] SOC1\n",                  !!(f & FLAG_SOC1));
    printf("     [%d] SOCF\n",                  !!(f & FLAG_SOCF));
    printf("     [%d] DSG (discharging)\n",     !!(f & FLAG_DSG));
    printf("-------------------------------------------\n");
    return 0;
}







static volatile int g_stop = 0;
static void handle_sigint(int sig) 
{ 
    (void)sig; 
    g_stop = 1; 
}

/* returns 1 if any Ra element differs */
static int ra_differs(const uint16_t a[15], const uint16_t b[15]) 
{
    for (int i = 0; i < 15; i++) if (a[i] != b[i]) return 1;
    return 0;
}

int bq_learning_monitor(bq27426_t *ctx, unsigned period_ms, unsigned max_minutes)
{
    /* set sane defaults */
    if (!period_ms) period_ms = 3000;

    /* Ctrl+C support */
    signal(SIGINT, handle_sigint);

    /* capture starting state */
    uint8_t  soc = 0;
    uint16_t qmax0 = 0, qmax_now = 0;
    uint16_t ra0[15] = {0}, ra_now[15] = {0};
    uint16_t flags = 0, cstat = 0, volt = 0;
    int16_t  curr = 0, pwr = 0;
    uint16_t t01k = 0;

    /* Read baselines */
    bq_qmax_read(ctx, &qmax0);
    bq_ra_table_read(ctx, ra0);

    // /* Tell user where we start */
    // bq_control(ctx, 0x0000);
    // bq_rd16(ctx, BQ27426_CMD_CNTL, &cstat);

    bq_rd16(ctx, 0x06, &flags);
    printf("\n--- Learning Monitor Started ---\n");

    printf("Initial Qmax   : %u mAh\n", qmax0);
    printf("Initial FC/DSG : FC=%d, DSG=%d\n",!!(flags & (1<<9)), !!(flags & (1<<0)));

    printf("Stop criteria  : See FC, then DSG, and (Qmax change OR any Ra change)\n");

    printf("Polling every  : %u ms (Ctrl+C to stop)\n\n", period_ms);

    /* Track milestones */
    int saw_fc  = !!(flags & (1<<9));   /* already full when we start? */
    int saw_dsg = !!(flags & (1<<0));

    const time_t t_start = time(NULL);
    const time_t t_deadline = max_minutes ? (t_start + (time_t)(max_minutes*60)) : 0;

    /***********************************************************/

    // force exit config mode cleanly
    bq_exit_cfg(ctx, 1);      // resim = 1 (soft reset / resim)
    usleep(300000);          // 300 ms settle
    // now re-check FLAGS
    bq_rd16(ctx, BQ27426_CMD_FLAGS, &flags);
    printf("CFGUPMODE bit is now: %d\n",!!(flags & (1 << 4)));

    /*************************************************************/

    bq_dump_control_status(ctx);
    bq_dump_flags(ctx);

    while (!g_stop) 
    {
        bq_rd16(ctx, 0x06, &flags);
        // bq_control(ctx, 0x0000);
        // bq_rd16(ctx, BQ27426_CMD_CNTL, &cstat);

        bq_voltage_mV(ctx, &volt);
        bq_current_mA(ctx, &curr);
        bq_power_mW(ctx, &pwr);
        bq_soc_pct(ctx, &soc);
        bq_temp_cell_c01K(ctx, &t01k);

        // bq_qmax_read(ctx, &qmax_now);
        // bq_ra_table_read(ctx, ra_now);

        /* Print a single status line */
        printf("V=%4u mV | I=%5d mA | P=%6d mW | SOC=%3u%% | T=%.1f C | "
               "FC=%d DSG=%d\n",
               volt, curr, pwr, soc, (bq_k01_to_c(t01k)), 
               !!(flags & (1<<9)), !!(flags & (1<<0)));

        /* Detect milestones */
        if (flags & (1<<9))  saw_fc  = 1;  /* FC */
        if (flags & (1<<0))  saw_dsg = 1;  /* DSG */

        /* Check learning changes */
        // int qmax_changed = (qmax_now != qmax0);
        // int ra_changed   = ra_differs(ra0, ra_now);

        /* Optional: log when changes first appear */
        // static int logged_qmax = 0, logged_ra = 0;
        // if (qmax_changed && !logged_qmax) 
        // {
        //     printf(">>> Qmax changed: %u -> %u mAh\n", qmax0, qmax_now);
        //     logged_qmax = 1;
        // }
        // if (ra_changed && !logged_ra) 
        // {
        //     printf(">>> Ra table changed\n");
        //     for (int i = 0; i < 15; i++) 
        //     {
        //         if (ra0[i] != ra_now[i])
        //             printf("    Ra[%02d]: %u -> %u\n", i, ra0[i], ra_now[i]);
        //     }

        //     logged_ra = 1;
        // }

        /* Stop condition:
           - We’ve seen FC at some point (top of charge),
           - We’ve seen DSG (discharging),
           - And either Qmax changed or Ra changed. */
        if (saw_fc && saw_dsg) 
        {
            printf("\n--- Learning monitor: stop condition met ---\n");
            break;
        }

        // /* Timeout? */
        // if (t_deadline && time(NULL) > t_deadline) {
        //     printf("\n--- Learning monitor: timed out after %u minute(s) ---\n", max_minutes);
        //     break;
        // }

        /* Sleep */
        usleep(period_ms * 1000U);
    }

    if (g_stop) puts("\n--- Learning monitor: interrupted by user ---");

    /* Final dump */
    puts("\nFinal CONTROL_STATUS / FLAGS:");
    bq_dump_control_status(ctx);
    bq_dump_flags(ctx);

    puts("Final Qmax / Ra:");
    bq_print_qmax_and_ra(ctx);

    return 0;
}

int bq_get_chem_id(bq27426_t *ctx, uint16_t *chem_id)
{
    if (!ctx || !chem_id) return -1;

    /* Issue CHEM_ID control subcommand */
    if (bq_control(ctx, CNTL_CHEM_ID) < 0) 
    {
        printf("[BQ] ChemID subcmd failed\n");
        return -1;
    }

    /* Small dwell so the gauge places the result at 0x00/0x01 */
    usleep(5000);

    /* Read raw bytes at Control() registers 0x00/0x01 */
    uint8_t raw[2];
    uint8_t reg = BQ27426_CMD_CNTL; /* 0x00 */
    if (write(ctx->fd, &reg, 1) != 1) 
    {
        printf("error:chemID write failed\n");
        return -1;
    }

    if (read(ctx->fd, raw, 2) != 2) 
    {
        printf("error:ChemID: read(2) failed\n");
        return -1;
    }

    printf("[BQ][CHEM] raw bytes: 0x%02X 0x%02X\n", raw[0], raw[1]);
    *chem_id = (uint16_t)(raw[0] | (raw[1] << 8));
    printf("[BQ] -> ChemID current: 0x%04X\n", *chem_id);
    return 0;
}


int bq_set_chem_id(bq27426_t *ctx, uint16_t chem_id)
{
    uint8_t be[2] = { (chem_id >> 8), (chem_id & 0xFF) };

    printf("[BQ] Setting ChemID = 0x%04X ...\n", chem_id);

    if (bq_unseal_try(ctx) < 0) { printf("error unseal\n"); return -1; }
    if (bq_enter_cfg(ctx) < 0)  { printf("error enter_cfg\n"); return -1; }

    if (bq_write_extended(ctx, CLASS_CHEM_ID, 0, be, 2) < 0) 
    {
        printf("error ChemID write failed\n");
        bq_exit_cfg(ctx, 1);
        return -1;
    }

    bq_exit_cfg(ctx, 0);
    bq_wait_ready(ctx, 5000);
    // bq_make_sealed(ctx);

    printf("[BQ] -> ChemID 0x%04X set successfully.\n", chem_id);
    return 0;
}

// Map the 3 chemistry subcommands
#define CNTL_CHEM_A 0x0030  // 0x3230
#define CNTL_CHEM_B 0x0031  // 0x1202  <-- 4.20 V
#define CNTL_CHEM_C 0x0032  // 0x3142

int bq_set_chem_1202(bq27426_t *ctx)
{
    uint8_t flags[2];
    int tries;

    printf("[BQ] Change chemistry -> CHEM_B (0x1202)\n");

    if (bq_unseal_try(ctx) < 0) 
    {
        printf("error :unseal\n");
        return -1;
    }   

    if (bq_enter_cfg(ctx)   < 0) 
    {
        printf("error: enter_cfg");
        return -1;
    }


    // 3) Wait Flags[CFGUPMODE] (bit4) = 1
    for (tries = 0; tries < 10; tries++) {
        uint8_t reg = 0x06;
        if (write(ctx->fd, &reg, 1) == 1 && read(ctx->fd, flags, 2) == 2) {
            if (flags[0] & 0x10) break;
        }
        usleep(100000);
    }

    // 4) Send CHEM_B subcommand (select profile 0x1202)
    if (bq_control(ctx, CNTL_CHEM_B) < 0) return -1;
    usleep(50000);

    // 5) Exit CFGUPDATE via SOFT_RESET

    (void)bq_exit_cfg(ctx, /*resim*/0);
    // if (bq_control(ctx, CNTL_SOFT_RESET) < 0) return -1;

    // 6) Wait until CFGUPMODE clears

    // usleep(100000);
    // for (tries = 0; tries < 10; tries++) 
    // {
    //     uint8_t reg = 0x06;
    //     if (write(ctx->fd, &reg, 1) == 1 && read(ctx->fd, flags, 2) == 2) 
    //     {
    //         if (!(flags[0] & 0x10)) break;
    //     }
    //     usleep(100000);
    // }

    // 7) Verify ChemID via Control(CHEM_ID=0x0008)
    uint16_t chem = 0;
    if (bq_get_chem_id(ctx, &chem) == 0)
        printf("[BQ] ChemID now: 0x%04X %s\n", chem, (chem==0x1202)?"OK":"(unexpected)");

    // 8) Seal (optional)
    bq_make_sealed(ctx);

    return (chem == 0x1202) ? 0 : -1;
}
