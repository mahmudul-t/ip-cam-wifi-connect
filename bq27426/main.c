#include "bq27426.h"
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    bq27426_data_t data;

    while (1) 
    {
        if (bq27426_read_data(BQ27426_I2C_BUS, BQ27426_ADDR, &data) == 0)
        {
            bq27426_print_data(&data);
            printf("\n\nvotage   = %d\n\n", data.voltage);
        }
        else
            printf("Failed to read from bq27426\n");

        sleep(5); // refresh every 2 seconds
    }

    return 0;
}
