// button_monitor.c
// Build:  gcc -std=c11 -O2 -Wall -Wextra -o button_monitor button_monitor.c
// Run as root.

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define GPIO_NUM        "64"
#define GPIO_PATH       "/sys/class/gpio/gpio64/"
#define AP_MODE_SCRIPT  "/system/www/ap_mode_enable.sh"

#define DEBOUNCE_MS     80      // debounce after interrupt
#define HOLD_SECONDS    2       // long-press duration
#define COOLDOWN_SEC    20      // avoid retriggering too fast

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int s){ (void)s; g_stop = 1; }

static void msleep_monotonic(int ms){
    struct timespec ts = { ms/1000, (ms%1000)*1000000L };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        if (g_stop) break;
    }
}

static int write_line(const char *path, const char *val){
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    int rc = dprintf(fd, "%s\n", val); // newline helps some sysfs drivers
    int e = errno;
    close(fd);
    if (rc < 0) { errno = e; return -1; }
    return 0;
}

static int read_char(const char *path, char *out){
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    char c = 0;
    if (read(fd, &c, 1) != 1) { close(fd); return -1; }
    close(fd);
    *out = c;
    return 0;
}

static void export_gpio(void){
    // If already exported, this may fail with EBUSY — that's fine.
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd >= 0) {
        (void)write(fd, GPIO_NUM, strlen(GPIO_NUM));
        close(fd);
    }
    usleep(200000); // wait 200ms for sysfs to populate
}

static void unexport_gpio(void){
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd >= 0) {
        (void)write(fd, GPIO_NUM, strlen(GPIO_NUM));
        close(fd);
    }
}

static void set_gpio_direction_in(void){
    // Some controllers reject changing direction via sysfs; ignore EINVAL.
    if (write_line(GPIO_PATH "direction", "in") != 0 && errno != EINVAL) {
        perror("set direction");
    }
}

static void set_gpio_edge_falling(void){
    if (write_line(GPIO_PATH "edge", "falling") != 0) {
        perror("set edge");
    }
}

static int read_gpio_value(void){
    int fd = open(GPIO_PATH "value", O_RDONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    char c = '1';
    if (read(fd, &c, 1) != 1) { close(fd); return -1; }
    close(fd);
    return (c == '0') ? 0 : 1; // 0 = pressed (active-low)
}

static int run_ap_script(void){
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", AP_MODE_SCRIPT, (char*)NULL);
        _exit(127); // exec failed
    }
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    return 0; // parent returns immediately
}

int main(void){
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGCHLD, SIG_IGN); // avoid zombies if script exits

    export_gpio();
    set_gpio_direction_in();     // tolerant if kernel refuses
    set_gpio_edge_falling();

    int fd = open(GPIO_PATH "value", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Failed to open GPIO value");
        return 1;
    }

    struct pollfd pfd = { .fd = fd, .events = POLLPRI | POLLERR };
    char buf[16];

    // Clear any pending interrupt before polling
    (void)lseek(fd, 0, SEEK_SET);
    (void)read(fd, buf, sizeof(buf));

    printf("[BOOT BTN] Monitoring GPIO%s (falling edge, long-press %ds)...\n",
           GPIO_NUM, HOLD_SECONDS);
    fflush(stdout);

    while (!g_stop) {
        int ret = poll(&pfd, 1, -1);
        if (ret < 0) {
            if (errno == EINTR && g_stop) break;
            perror("poll");
            continue;
        }
        if (!(pfd.revents & POLLPRI)) continue;

        // Clear interrupt by reading value
        (void)lseek(fd, 0, SEEK_SET);
        (void)read(fd, buf, sizeof(buf));

        // Quick debounce
        msleep_monotonic(DEBOUNCE_MS);

        // Long-press: wait HOLD_SECONDS and confirm still pressed (active-low)
        sleep(HOLD_SECONDS);
        int v = read_gpio_value();
        if (v < 0) {
            perror("read_gpio_value");
            continue;
        }

        if (v == 0) {
            printf("[BOOT BTN] Long press detected → launching: %s\n", AP_MODE_SCRIPT);
            fflush(stdout);
            (void)run_ap_script();
            // Cooldown to prevent rapid re-triggering while button is held
            sleep(COOLDOWN_SEC);
        } else {
            printf("[BOOT BTN] Short press ignored.\n");
            fflush(stdout);
        }
    }

    close(fd);
    unexport_gpio(); // optional; comment out if other processes use it
    printf("[BOOT BTN] Exiting.\n");
    return 0;
}
