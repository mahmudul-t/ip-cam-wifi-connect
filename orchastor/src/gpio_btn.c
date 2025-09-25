// ========================= src/gpio_btn.c =========================
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <poll.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include "config.h"
#include "log.h"

static int read_val_now(void) {
    int fd = open(GPIO_DIRPATH "value", O_RDONLY);
    if (fd < 0) return -1;
    char b='1'; (void)read(fd, &b, 1); close(fd);
    return (b == '0') ? 0 : 1; // 0 == pressed (active-low)
}

int gpio_init(void) {
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd >= 0) { (void)write(fd, GPIO_NUM_STR, strlen(GPIO_NUM_STR)); close(fd); }
    usleep(200000);
    fd = open(GPIO_DIRPATH "direction", O_WRONLY);
    if (fd >= 0) { (void)write(fd, "in", 2); close(fd); }
    fd = open(GPIO_DIRPATH "edge", O_WRONLY);
    if (fd >= 0) { (void)write(fd, "falling", 7); close(fd); }
    logf_tag("boot-btn", "monitoring GPIO%s for long press...", GPIO_NUM_STR);
    return 0;
}

int gpio_wait_long_press(void) {
    int fd = open(GPIO_DIRPATH "value", O_RDONLY);
    if (fd < 0) { perror("open GPIO value"); return 0; }

    struct pollfd pfd = { .fd = fd, .events = POLLPRI | POLLERR };
    char dummy[8];

    // clear old value to arm edge
    lseek(fd, 0, SEEK_SET); (void)read(fd, dummy, sizeof(dummy));

    int ret = poll(&pfd, 1, -1);
    if (ret <= 0 || !(pfd.revents & POLLPRI)) { close(fd); return 0; }

    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    long long t0_ms = (long long)ts.tv_sec*1000 + ts.tv_nsec/1000000;

    usleep(50*1000); // debounce

    long long pressed_ms = 0;
    while (pressed_ms < BUTTON_LONG_MS) {
        int v = read_val_now();
        if (v != 0) { close(fd); logf_tag("boot-btn", "short press ignored"); return 0; }
        usleep(50*1000);
        clock_gettime(CLOCK_MONOTONIC, &ts);
        long long now_ms = (long long)ts.tv_sec*1000 + ts.tv_nsec/1000000;
        pressed_ms = now_ms - t0_ms;
    }

    close(fd);
    logf_tag("boot-btn", "LONG press detected");
    return 1;
}
