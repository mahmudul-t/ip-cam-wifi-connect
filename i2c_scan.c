// i2c_scan.c — scan /dev/i2c-2 by default, addr range [START..END]
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef START_ADDR
#define START_ADDR 0x03   // set to 0x00 if you want truly all
#endif
#ifndef END_ADDR
#define END_ADDR   0x77   // set to 0x7f for all 7-bit values
#endif

static int probe_addr(int fd, int addr) 
{
    // Try an SMBus QUICK-like probe using I2C_RDWR: a 0-byte write isn’t valid,
    // so do a harmless 1-byte read; many devices NACK if no register set—OK.
    struct i2c_rdwr_ioctl_data rdwr = {0};
    uint8_t byte = 0;

    struct i2c_msg msg = 
    {
        .addr  = addr,
        .flags = I2C_M_RD,
        .len   = 1,
        .buf   = &byte,
    };

    rdwr.msgs  = &msg;
    rdwr.nmsgs = 1;
    return ioctl(fd, I2C_RDWR, &rdwr);  // 0 on success, -1 on NACK/err
}

int main(int argc, char **argv) 
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/i2c-2";

    int fd = open(dev, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    printf("Scanning %s Starting address: 0x%02x and ending address: 0x%02x \n", dev, START_ADDR, END_ADDR);

    printf("    00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f\n");

    for (int base = 0; base < 0x80; base += 0x10) 
    {
        printf("%02x: ", base);

        for (int a = base; a < base + 0x10; a++) 
        {
            if (a < START_ADDR || a > END_ADDR) 
            { 
                printf("   "); 
                continue; 
            }
            if (ioctl(fd, I2C_SLAVE, a) < 0) 
            { 
                printf("-- "); 
                continue; 
            }
            
            if (probe_addr(fd, a) >= 0) 
                printf("%02x ", a);
            else                         
                printf("-- ");
        }
        
        printf("\n");
    }
    close(fd);
    return 0;
}
