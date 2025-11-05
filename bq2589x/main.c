#include <stdio.h>
#include <unistd.h>
#include "bq2589x.h"

#include <pthread.h>
#include <unistd.h>    
  
#include <time.h>      


static void* th_bat_charger(void * arg);

int init_bat_monitor_th()
{
    pthread_t bat_charger_thread;

    pthread_create(&bat_charger_thread, NULL, th_bat_charger, NULL);

    return 1;
}



static void* th_bat_charger(void * arg)
{
    (void)arg; 

    bq2589x_dev charger;
    bq2589x_part_no pn;
    int rev;


    int bat_mv, sys_mv ,vbus_mv ,temp , ichg_ma, status;

    if (bq2589x_init(&charger, BQ25895_ADDR) != BQ2589X_OK) 
    {
        fprintf(stderr, "Failed to init BQ25895\n");
        pthread_exit(NULL);
    }

    // Detect device
    if (bq2589x_detect_device(&charger, &pn, &rev) == BQ2589X_OK)
    {
        printf("Detected BQ2589x PN=0x%02x Rev=%d\n", pn, rev);
    }
    else 
    {
        printf("No device found\n");
        bq2589x_deinit(&charger);
        pthread_exit(NULL);
    }

    
    if (bq2589x_adc_start(&charger, false) == BQ2589X_OK) 
    {
        printf("adc start\n");
        usleep(15000);  
    }

   
    bq2589x_enable_charger(&charger);   //Enable Charger
    bq2589x_disable_otg(&charger);  // disable otg
    bq2589x_disable_watchdog_timer(&charger); // disable watchdog
    bq2589x_force_dpdm(&charger); // Force DPDM for correct VBUS type 
    usleep(500000);  // Wait 500ms
    bq2589x_set_charge_current(&charger, 832); // current limit 832mA
    bq2589x_set_term_current(&charger, 128); // Termination at ~128 mA
    bq2589x_set_prechg_current(&charger, 128); // Precharge current for deeply discharged cells
    bq2589x_set_chargevoltage(&charger, 4200); // 4.20 V charge voltage
    bq2589x_set_input_volt_limit(&charger, 4400); // Absolute input voltage limit (VINDPM abs)
    bq2589x_set_input_current_limit(&charger, 1500); // Adapter input current limit (1.5 A)
    bq2589x_set_vindpm_offset(&charger, 500); // Dynamic VINDPM offset from no-load VBUS (0.5 V)

    while(1)
    {
        // --- Read Values ---
        bat_mv = bq2589x_adc_read_battery_volt(&charger);
        sys_mv = bq2589x_adc_read_sys_volt(&charger);
        vbus_mv = bq2589x_adc_read_vbus_volt(&charger);
        temp = bq2589x_adc_read_temperature(&charger);
        ichg_ma = bq2589x_adc_read_charge_current(&charger);
        status = bq2589x_get_charging_status(&charger);
        bq2589x_vbus_type vbus_type = bq2589x_get_vbus_type(&charger);

        printf("VBUS: %d mV | ", vbus_mv);
        printf("Battery: %d mV |  ", bat_mv);
        printf("System: %d mV |  ", sys_mv);
        printf("Temp:   %.1f °C |  ", temp / 10.0);
        printf("Charge I: %d mA | ", ichg_ma);
        printf("Status: %d ", status);
        switch(status) {
            case 0: printf("(Not Charging) |  "); break;
            case 1: printf("(Pre-charge)  |  "); break;
            case 2: printf("(Fast Charge)  |  "); break;
            case 3: printf("(Charge Done)  |  "); break;
            default: printf("(Error)  |  "); break;
        }
        printf("VBUS Type: %d ", vbus_type);
        switch(vbus_type) {
            case BQ2589X_VBUS_USB_SDP:  printf("(USB SDP)\n"); break;
            case BQ2589X_VBUS_USB_CDP:  printf("(USB CDP)\n"); break;
            case BQ2589X_VBUS_USB_DCP:  printf("(Wall Charger)\n"); break;
            case BQ2589X_VBUS_MAXC:     printf("(MaxCharge)\n"); break;
            default: printf("(Unknown/None)\n"); break;
        }

        sleep(5);
    }

    bq2589x_deinit(&charger);
    // return 0;
}


int main(void)
{
   init_bat_monitor_th(); 

   while(1)
   {
    sleep(5000);
   }

   return 0;

}