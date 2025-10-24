// #include "bq27426_t23.h"
// #include <stdio.h>
// #include <unistd.h> // for sleep()

// int main(void)
// {
//     bq27426_t g;

//     /* Open I2C device */
//     if (bq27426_open(&g, BQ27426_DEFAULT_DEV, BQ27426_DEFAULT_ADDR) < 0) {
//         perror("open bq27426");
//         return 1;
//     }

//     printf("=====================================\n");
//     printf("  BQ27426 Fuel Gauge Monitor (T23)   \n");
//     printf("=====================================\n");

//     /* ---- Set Design Parameters ---- */
//     printf("Setting battery design parameters...\n");
//     bq_set_design_capacity(&g, 3600);   // mAh
//     bq_set_design_energy(&g,   11400);  // mWh  (≈ 3.8 V × 3 Ah)
//     bq_set_terminate_voltage(&g, 3400); // mV
//     bq_set_taper_rate(&g, 1000);        // 0.1 h units
//     printf("Design parameters configured.\n\n");

    

//     usleep(100000);  // 100 ms

//     bq_verify_state_params_verbose(&g, 3600, 11400, 3400, 1000);

//     printf("make sealed\n\n");
//     bq_make_sealed(&g);

//     /* ---- Continuous Monitoring ---- */
//     while (1) {
//         uint16_t voltage_mV, remCap_mAh, temp_01K;
//         int16_t current_mA, power_mW;
//         uint8_t soc, soh;
//         uint16_t fullCap, nomCap, availCap;


//         if (bq_voltage_mV(&g, &voltage_mV) == 0 &&
//             bq_current_mA(&g, &current_mA) == 0 &&
//             bq_power_mW(&g, &power_mW) == 0 &&
//             bq_soc_pct(&g, &soc) == 0 &&
//             bq_soh_pct(&g, &soh) == 0 &&
//             bq_temp_cell_c01K(&g, &temp_01K) == 0 &&
//             bq_capacity_remain_mAh(&g, &remCap_mAh) == 0 &&
//             bq_capacity_full_mAh(&g, &fullCap) == 0 &&
//             bq_capacity_nom_mAh(&g, &nomCap) == 0 &&
//             bq_capacity_avail_mAh(&g, &availCap) == 0 )
//         {
//             printf("Volt=%4u mV | Curr=%5d mA | Power=%6d mW | "
//                    "SOC=%3u%% | SOH=%3u%% | Temp=%.1f °C | RemCap=%4u mAh |  "
//                    "fullC=%4u mAh | nomC=%4u mAh | availC=%4u mAh\n",
//                    voltage_mV, current_mA, power_mW,
//                    soc, soh, bq_k01_to_c(temp_01K), remCap_mAh,
//                     fullCap, nomCap, availCap);
//         }
//         else {
//             printf("[!] read error\n");
//         }

//         fflush(stdout);
//         sleep(3);
//     }

//     bq27426_close(&g);
//     return 0;
// }



// main.c
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "bq27426_t23.h"

// --- choose your I2C bus and address here ---
#define I2C_DEV   "/dev/i2c-2"
#define I2C_ADDR  0x55

static volatile int g_run = 1;
static void on_sigint(int sig) 
{ 
    (void)sig; 
    g_run = 0; 
}

static void print_line(bq27426_t *g)
{
    uint16_t mv=0, t01k=0, rem=0, full=0, nom=0, avail=0;
    int16_t  ma=0, mw=0;
    uint8_t  soc=0, soh=0;

    (void)bq_voltage_mV(g, &mv);
    (void)bq_current_mA(g, &ma);
    (void)bq_power_mW(g, &mw);
    (void)bq_soc_pct(g, &soc);
    (void)bq_soh_pct(g, &soh);
    (void)bq_temp_cell_c01K(g, &t01k);
    (void)bq_capacity_remain_mAh(g, &rem);
    (void)bq_capacity_full_mAh(g,   &full);
    (void)bq_capacity_nom_mAh(g,    &nom);
    (void)bq_capacity_avail_mAh(g,  &avail);

    printf("Volt=%4u mV | Curr=%5d mA | Power=%6d mW | "
           "SOC=%3u%% | SOH=%3u%% | Temp=%.1f °C | "
           "RemCap=%4u mAh | fullC=%4u mAh | nomC=%4u mAh | availC=%4u mAh\n",
           mv, ma, mw, soc, soh, bq_k01_to_c(t01k),
           rem, full, nom, avail);
}

int main(int argc, char **argv)
{
    // simple flags:
    //   --program   : write design params first
    //   --seal      : seal after programming/verify
    //   --skipmon   : skip learning monitor
    int do_program = 0, do_seal = 0, skip_monitor = 0;

    for (int i = 1; i < argc; i++) 
    {
        if (!strcmp(argv[i], "--program")) do_program = 1;
        else if (!strcmp(argv[i], "--seal")) do_seal = 1;
        else if (!strcmp(argv[i], "--skipmon")) skip_monitor = 1;
        else 
        {
            fprintf(stderr, "usage: %s [--program] [--seal] [--skipmon]\n", argv[0]);
            return 2;
        }
    }

    bq27426_t g;
    if (bq27426_open(&g, I2C_DEV, I2C_ADDR) < 0) {
        perror("bq27426_open");
        return 1;
    }

    printf("====================================\n");
    printf("  BQ27426 Fuel Gauge Monitor (T23)  \n");
    printf("====================================\n");

    bq_set_chem_1202(&g);

    // Optionally program design parameters once
    //     uint16_t chemid = 0;
    // if (bq_get_chem_id(&g, &chemid) == 0) 
    // {
    //     if (chemid != DEFAULT_CHEMID) 
    //     {
    //         printf("[BQ] ChemID mismatch (0x%04X). Updating to 0x%04X ...\n", chemid, DEFAULT_CHEMID);
    //         bq_set_chem_id(&g, DEFAULT_CHEMID);
    //         sleep(1);
    //         bq_get_chem_id(&g, &chemid); // read back to confirm
    //     } 
    //     else 
    //     {
    //         printf("[BQ] ChemID already correct (0x%04X)\n", chemid);
    //     }
    // } 
    // else 
    // {
    //     printf("[BQ] Failed to read ChemID\n");
    // }

    if (do_program || 1) 
    {
        printf("Setting battery design parameters...\n");
        if (bq_set_design_capacity(&g,   3600) < 0) perror("set design cap");
        if (bq_set_design_energy(&g,    13320) < 0) perror("set design energy");
        if (bq_set_terminate_voltage(&g, 3400) < 0) perror("set term volt");
        if (bq_set_taper_rate(&g,        200) < 0) perror("set taper");
        printf("Design parameters configured.\n");
    }

    // Verify (read-back) the design parameters
    (void)bq_verify_state_params_verbose(&g, 3600, 13320, 3400, 200);



    // One-shot status dump
    bq_dump_control_status(&g);
    bq_dump_flags(&g);

    // Optional: seal after programming/verify
    if (do_seal) {
        puts("\nSealing gauge...");
        (void)bq_make_sealed(&g);
        // show status after sealing
        bq_dump_control_status(&g);
    }

    // Optional learning monitor: watches FC/DSG/Qmax/Ra
    if (!skip_monitor) 
    {
        // Put gauge into learning mode (optional but helpful)
        bq_set_learning_mode(&g, BQ_LEARN_ENABLE);
        // poll every 3s, stop after 90 minutes if no stop-condition met
        bq_learning_monitor(&g, 3000, 90);
        bq_set_learning_mode(&g, BQ_LEARN_FREEZE_UNSEALED);
    }

    // Continuous live print every 3s until Ctrl-C
    signal(SIGINT, on_sigint);
    puts("\n--- Live stream (Ctrl-C to stop) ---");
    while (g_run) {
        print_line(&g);
        usleep(3000 * 1000);
    }

    bq27426_close(&g);
    puts("bye!");
    return 0;
}
