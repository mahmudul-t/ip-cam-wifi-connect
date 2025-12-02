#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <pthread.h>

#define GPIO_NUM           "64"
#define GPIO_PATH          "/sys/class/gpio/gpio64/"
#define AP_MODE_SCRIPT     "/system/www/ap_mode_enable.sh"
#define ENABLE_WIFI_SCRIPT "/system/www/enable_wifi.sh"
#define APP_NAME           "keo-cam"   // Application started by enable_wifi.sh

static int   ap_mode_active = 0; // 0 = STA mode, 1 = AP mode
static pid_t ap_pid         = -1;
static pid_t wifi_pid       = -1;

/* ========================= GPIO HELPERS ========================= */

static void export_gpio(void)
{
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd >= 0) {
        write(fd, GPIO_NUM, strlen(GPIO_NUM));
        close(fd);
    }
    usleep(200000);  // wait 200ms for sysfs to populate
}

static void unexport_gpio(void)
{
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd >= 0) {
        write(fd, GPIO_NUM, strlen(GPIO_NUM));
        close(fd);
    }
}

static void set_gpio_direction(void)
{
    int fd = open(GPIO_PATH "direction", O_WRONLY);
    if (fd >= 0) {
        write(fd, "in", 2);
        close(fd);
    }
}

static void set_gpio_edge(void)
{
    int fd = open(GPIO_PATH "edge", O_WRONLY);
    if (fd >= 0) {
        write(fd, "falling", 7);  // falling edge = button pressed (active low)
        close(fd);
    }
}

static int read_gpio_value(void)
{
    int fd = open(GPIO_PATH "value", O_RDONLY);
    if (fd < 0) return -1;

    char buf;
    if (read(fd, &buf, 1) != 1) {
        close(fd);
        return -1;
    }

    close(fd);
    return (buf == '0') ? 0 : 1;
}

/* ====================== SCRIPT CONTROL HELPERS ====================== */

static int run_ap_script(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", AP_MODE_SCRIPT, (char *)NULL);
        _exit(127); // exec failed
    }
    if (pid < 0) {
        perror("fork (AP)");
        return -1;
    }

    ap_pid = pid;
    return 0;
}

static int run_wifi_script(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", ENABLE_WIFI_SCRIPT, (char *)NULL);
        _exit(127); // exec failed
    }
    if (pid < 0) {
        perror("fork (WIFI)");
        return -1;
    }

    wifi_pid = pid;
    return 0;
}

static int stop_wifi_script(void)
{
    if (wifi_pid > 0) {
        if (kill(wifi_pid, SIGTERM) == 0) {
            printf("Sent SIGTERM to WIFI script pid=%d\n", wifi_pid);
            wifi_pid = -1;
            return 0;
        } else {
            perror("kill (wifi_pid)");
            return -1;
        }
    }
    return -1; // no known wifi_pid
}

static int stop_application(void)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "pgrep %s", APP_NAME);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        perror("popen (pgrep)");
        return -1;
    }

    char pid_str[16];
    if (fgets(pid_str, sizeof(pid_str), fp) != NULL) {
        pid_t pid = (pid_t)atoi(pid_str);
        printf("Found %s PID: %d\n", APP_NAME, pid);

        if (kill(pid, SIGTERM) == 0) {
            printf("Sent SIGTERM to %s\n", APP_NAME);
        } else {
            perror("kill (APP)");
        }
    } else {
        printf("%s not running\n", APP_NAME);
    }

    pclose(fp);
    return 0;
}

/* ====================== WIFI STATUS / RECONNECT ====================== */

typedef enum {
    WIFI_STATE_UNKNOWN = 0,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_COMPLETED
} wifi_state_t;

typedef struct {
    wifi_state_t state;
    char ip[64];
} wifi_status_t;

// Run a shell command and capture output into buffer
static int run_cmd_capture(const char *cmd, char *out, size_t out_len)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    size_t total = 0;
    out[0] = '\0';

    while (fgets(out + total, out_len - total, fp)) {
        total = strlen(out);
        if (total >= out_len - 1) break;
    }

    pclose(fp);
    return 0;
}

// Fill wifi_status_t by parsing `wpa_cli status`
static void wifi_get_status(wifi_status_t *st)
{
    memset(st, 0, sizeof(*st));
    st->state = WIFI_STATE_UNKNOWN;

    char buf[2048] = {0};

    if (run_cmd_capture("/system/tools/wifi/wpa_cli -i wlan0 status", buf, sizeof(buf)) != 0) {
        return;
    }

    char *saveptr;
    char *line = strtok_r(buf, "\n", &saveptr);
    while (line) {
        if (strncmp(line, "wpa_state=", 10) == 0) {
            const char *v = line + 10;
            if (strcmp(v, "COMPLETED") == 0) {
                st->state = WIFI_STATE_COMPLETED;
            } else {
                // Treat any non-COMPLETED as "not ready"
                st->state = WIFI_STATE_DISCONNECTED;
            }
        } else if (strncmp(line, "ip_address=", 11) == 0) {
            snprintf(st->ip, sizeof(st->ip), "%s", line + 11);
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }
}

static void wifi_run_dhcp(void)
{
    printf("[WiFi] Running DHCP via udhcpc...\n");
    system("udhcpc -i wlan0 -n -t 3 -T 2 >/dev/console 2>&1");
}

// Wait until wpa_state=COMPLETED or timeout_ms
static int wifi_wait_for_completed(int timeout_ms)
{
    const int step_ms = 500;
    int waited = 0;
    wifi_status_t st;

    while (waited < timeout_ms) {
        wifi_get_status(&st);

        if (st.state == WIFI_STATE_COMPLETED) {
            printf("[WiFi] State COMPLETED. ip=%s\n",
                   st.ip[0] ? st.ip : "(none)");

            // If wpa_cli has no IP, run DHCP
            if (st.ip[0] == '\0') {
                wifi_run_dhcp();
            }
            return 0;  // success
        }

        usleep(step_ms * 1000);
        waited += step_ms;
    }

    printf("[WiFi] Still not COMPLETED after %d ms\n", timeout_ms);
    return -1; // timeout
}

static void wifi_soft_reconnect(void)
{
    printf("[WiFi] Sending wpa_cli reconnect...\n");
    system("/system/tools/wifi/wpa_cli -i wlan0 reconnect >/dev/console 2>&1");
}

/* ====================== BUTTON THREAD ====================== */

static void *button_thread(void *arg)
{
    (void)arg;

    export_gpio();
    set_gpio_direction();
    set_gpio_edge();

    int fd = open(GPIO_PATH "value", O_RDONLY);
    if (fd < 0) {
        perror("Failed to open GPIO value");
        return NULL;
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

        int ret = poll(&pfd, 1, -1);  // block forever until event

        if (ret > 0 && (pfd.revents & POLLPRI)) {
            // Debounce / long press check
            sleep(2);

            if (read_gpio_value() == 0) {
                printf("[BOOT BTN] Long press detected. Stop WIFI + APP, launch AP mode...\n");
                stop_wifi_script();
                stop_application();
                run_ap_script();
                ap_mode_active = 1;
            } else {
                printf("[BOOT BTN] Short press ignored.\n");
            }
        }
    }

    close(fd);
    unexport_gpio();
    return NULL;
}

/* ====================== WIFI WATCHDOG THREAD ====================== */

static void *wifi_watchdog_thread(void *arg)
{
    (void)arg;

    wifi_status_t st;
    wifi_state_t last_state = WIFI_STATE_UNKNOWN;

    while (1) {
        sleep(10);  // check every 10s (tune as you like)

        if (ap_mode_active) {
            // In AP mode, do not try STA reconnect
            continue;
        }

        wifi_get_status(&st);

        if (st.state != last_state) {
            printf("[WiFi] State changed: %d -> %d, ip=%s\n",
                   last_state, st.state,
                   st.ip[0] ? st.ip : "(none)");
        }

        if (st.state != WIFI_STATE_COMPLETED) {
            printf("[WiFi] Not connected (state=%d). Trying reconnect...\n", st.state);

            wifi_soft_reconnect();

            if (wifi_wait_for_completed(10000) == 0) {
                printf("[WiFi] Reconnected successfully.\n");
            } else {
                printf("[WiFi] Reconnection failed or timed out.\n");
            }

            last_state = st.state;
            continue;
        }

        // Here: state == COMPLETED
        // Optional "internet" check using ping:
        int ret = system("ping -c 1 -W 1 8.8.8.8 >/dev/null 2>&1");
        if (ret != 0) {
            printf("[WiFi] COMPLETED but internet check failed. Re-running DHCP.\n");
            wifi_run_dhcp();
        } else {
            printf("[WiFi] COMPLETED and internet OK.\n");
        }

        last_state = st.state;
    }

    return NULL;
}

/* =============================== MAIN =============================== */

int main(void)
{
    pthread_t tid_btn, tid_wifi;

    // Start initial WiFi / application script
    if (run_wifi_script() != 0) {
        fprintf(stderr, "Failed to start WIFI script\n");
    }

    if (pthread_create(&tid_btn, NULL, button_thread, NULL) != 0) {
        perror("pthread_create button_thread");
    }

    if (pthread_create(&tid_wifi, NULL, wifi_watchdog_thread, NULL) != 0) {
        perror("pthread_create wifi_watchdog_thread");
    }

    pthread_join(tid_btn, NULL);
    pthread_join(tid_wifi, NULL);

    return 0;
}
