// boot_button_ap.c
// Build:  gcc -std=c11 -O2 -Wall -Wextra -o boot_button_ap boot_button_ap.c
// (On very old toolchains you may need:  ... -lrt )

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define GPIO_NUM        "64"   // change if needed
#define GPIO_DIR        "/sys/class/gpio"
#define GPIO_PATH       "/sys/class/gpio/gpio64/"
#define AP_MODE_SCRIPT  "/system/www/ap_mode_enable.sh"

// Behavior
#define DEBOUNCE_MS     80
#define COOLDOWN_MS     2000

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig){ (void)sig; g_stop = 1; }

static void msleep_monotonic(int ms) {
    struct timespec ts;
    ts.tv_sec = ms/1000;
    ts.tv_nsec = (ms%1000)*1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        if (g_stop) break;
    }
}

// return milliseconds between 'past' and 'now' (now >= past), monotonic
static long elapsed_ms_ts(struct timespec past, struct timespec now) {
    long sec  = (long)(now.tv_sec - past.tv_sec);
    long nsec = (long)(now.tv_nsec - past.tv_nsec);
    return sec * 1000L + nsec / 1000000L;
}

static int write_str(const char *path, const char *s) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    ssize_t n = write(fd, s, (size_t)strlen(s));
    close(fd);
    return (n == (ssize_t)strlen(s)) ? 0 : -1;
}

static int export_gpio_if_needed(void) {
    if (access(GPIO_PATH, F_OK) == 0) return 0;
    int fd = open(GPIO_DIR "/export", O_WRONLY);
    if (fd < 0) { perror("open export"); return -1; }
    if (write(fd, GPIO_NUM, strlen(GPIO_NUM)) < 0 && errno != EBUSY) {
        perror("write export");
        close(fd);
        return -1;
    }
    close(fd);
    for (int i = 0; i < 50; ++i) { // wait up to ~500ms
        if (access(GPIO_PATH, F_OK) == 0) return 0;
        msleep_monotonic(10);
    }
    fprintf(stderr, "Timeout waiting for %s\n", GPIO_PATH);
    return -1;
}

static int set_gpio_direction_in(void) {
    return write_str(GPIO_PATH "direction", "in");
}

static int set_gpio_edge_falling(void) {
    return write_str(GPIO_PATH "edge", "falling");
}

static int read_gpio_value_now(void) {
    int fd = open(GPIO_PATH "value", O_RDONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    char c = '1';
    if (read(fd, &c, 1) != 1) { close(fd); return -1; }
    close(fd);
    return (c == '0') ? 0 : 1; // 0=pressed (active-low), 1=released
}

static int run_ap_script(void) {
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", AP_MODE_SCRIPT, (char*)NULL);
        _exit(127); // exec failed
    }
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    return 0; // parent continues
}

int main(void) {
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGCHLD, SIG_IGN); // avoid zombies

    if (export_gpio_if_needed() != 0) return 1;
    if (set_gpio_direction_in() != 0) { perror("set direction"); return 1; }
    if (set_gpio_edge_falling() != 0) { perror("set edge"); return 1; }

    int fd = open(GPIO_PATH "value", O_RDONLY | O_NONBLOCK);
    if (fd < 0) { perror("open value"); return 1; }

    struct pollfd pfd = { .fd = fd, .events = POLLPRI | POLLERR };
    char buf[16];

    // clear any pending IRQ
    (void)lseek(fd, 0, SEEK_SET);
    (void)read(fd, buf, sizeof(buf));

    fprintf(stderr, "[BOOT BTN] Watching %s (GPIO%s) for press...\n", GPIO_PATH, GPIO_NUM);

    struct timespec last_launch = {0,0};

    while (!g_stop) {
        int ret = poll(&pfd, 1, -1);
        if (ret < 0) {
            if (errno == EINTR && g_stop) break;
            perror("poll");
            continue;
        }
        if (!(pfd.revents & POLLPRI)) continue;

        // clear interrupt
        (void)lseek(fd, 0, SEEK_SET);
        (void)read(fd, buf, sizeof(buf));

        // debounce
        msleep_monotonic(DEBOUNCE_MS);
        int v = read_gpio_value_now();
        if (v < 0) { perror("read value"); continue; }

        if (v == 0) { // still low => pressed
            // cooldown
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (last_launch.tv_sec != 0) {
                if (elapsed_ms_ts(last_launch, now) < COOLDOWN_MS) {
                    fprintf(stderr, "[BOOT BTN] Press ignored (cooldown).\n");
                    continue;
                }
            }

            fprintf(stderr, "[BOOT BTN] Press detected → launching: %s\n", AP_MODE_SCRIPT);
            if (run_ap_script() == 0) {
                last_launch = now;
            } else {
                fprintf(stderr, "[BOOT BTN] Failed to start AP script\n");
            }
        }
    }

    close(fd);
    fprintf(stderr, "[BOOT BTN] Exiting.\n");
    return 0;
}
