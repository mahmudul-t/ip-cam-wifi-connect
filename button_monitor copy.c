#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#define GPIO_NUM "64"
#define GPIO_PATH "/sys/class/gpio/gpio64/"
#define AP_MODE_SCRIPT "/system/www/ap_mode_enable.sh"

void export_gpio() {
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd >= 0) {
        write(fd, GPIO_NUM, strlen(GPIO_NUM));
        close(fd);
    }
    usleep(200000);  // wait 200ms for sysfs to populate
}

void unexport_gpio() {
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd >= 0) {
        write(fd, GPIO_NUM, strlen(GPIO_NUM));
        close(fd);
    }
}

void set_gpio_direction() {
    int fd = open(GPIO_PATH "direction", O_WRONLY);
    if (fd >= 0) {
        write(fd, "in", 2);
        close(fd);
    }
}

void set_gpio_edge() {
    int fd = open(GPIO_PATH "edge", O_WRONLY);
    if (fd >= 0) {
        write(fd, "falling", 7);  // falling edge = button pressed (active low)
        close(fd);
    }
}

int read_gpio_value() {
    int fd = open(GPIO_PATH "value", O_RDONLY);
    if (fd < 0) return -1;

    char buf;
    read(fd, &buf, 1);
    close(fd);
    return (buf == '0') ? 0 : 1;
}

int main() {
    export_gpio();
    set_gpio_direction();
    set_gpio_edge();

    int fd = open(GPIO_PATH "value", O_RDONLY);
    if (fd < 0) {
        perror("Failed to open GPIO value");
        return 1;
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLPRI | POLLERR;

    char buf[8];

    printf("[BOOT BTN] Monitoring GPIO64 for long press...\n");

    while (1) {
        // Clear old value
        lseek(fd, 0, SEEK_SET);
        read(fd, buf, sizeof(buf));

        int ret = poll(&pfd, 1, -1);  // wait forever for falling edge
        if (ret > 0 && (pfd.revents & POLLPRI)) {
            time_t press_start = time(NULL);

            // Wait 2 seconds and check if still pressed
            sleep(2);

            if (read_gpio_value() == 0) {
                printf("[BOOT BTN] Long press detected. Launching AP mode...\n");
                system(AP_MODE_SCRIPT);
                sleep(20);  // prevent rapid retriggering
            } else {
                printf("[BOOT BTN] Short press ignored.\n");
            }
        }
    }

    close(fd);
    unexport_gpio();
    return 0;
}
