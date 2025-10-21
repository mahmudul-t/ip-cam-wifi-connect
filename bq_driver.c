// bq27426_dump.c
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <unistd.h>

static int rd16(int fd, uint8_t reg, uint16_t *val) {
    if (write(fd, &reg, 1) != 1) return -1;
    uint8_t b[2];
    if (read(fd, b, 2) != 2) return -1;
    *val = (uint16_t)(b[0] | (b[1] << 8)); // little-endian
    return 0;
}

int main(int argc, char **argv) {
    const char *dev = (argc > 1) ? argv[1] : "/dev/i2c-2";
    int addr = 0x55;
    int fd = open(dev, O_RDWR); if (fd < 0) { perror("open"); return 1; }
    if (ioctl(fd, I2C_SLAVE, addr) < 0) { perror("ioctl I2C_SLAVE"); return 1; }

    uint16_t v;
    // Commands (TI bq27426)
    // 0x1C SOC %, 0x20 SOH (low byte = %), 0x04 Voltage mV, 0x10 AvgCurrent mA (signed)
    if (!rd16(fd, 0x1C, &v)) printf("SOC: %u %%\n", v & 0xFF);
    if (!rd16(fd, 0x20, &v)) printf("SOH: %u %%\n", v & 0xFF);
    if (!rd16(fd, 0x04, &v)) printf("Voltage: %u mV\n", v);
    if (!rd16(fd, 0x10, &v)) printf("AvgCurrent: %d mA\n", (int16_t)v);
    if (!rd16(fd, 0x02, &v)) printf("Temp: %u (0.1K) ≈ %.1f°C\n", v, (v - 2731)/10.0);

    return 0;
}
