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
#include <errno.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <errno.h>
#include <string.h>


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
    if (fd >= 0) 
    {
        write(fd, GPIO_NUM, strlen(GPIO_NUM));
        close(fd);
    }
    usleep(200000);  // wait 200ms for sysfs to populate
}

static void unexport_gpio(void)
{
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd >= 0) 
    {
        write(fd, GPIO_NUM, strlen(GPIO_NUM));
        close(fd);
    }
}

static void set_gpio_direction(void)
{
    int fd = open(GPIO_PATH "direction", O_WRONLY);
    if (fd >= 0) 
    {
        write(fd, "in", 2);
        close(fd);
    }
}

static void set_gpio_edge(void)
{
    int fd = open(GPIO_PATH "edge", O_WRONLY);
    if (fd >= 0) 
    {
        write(fd, "falling", 7);  // falling edge = button pressed (active low)
        close(fd);
    }
}

static int read_gpio_value(void)
{
    int fd = open(GPIO_PATH "value", O_RDONLY);
    if (fd < 0) return -1;

    char buf;
    if (read(fd, &buf, 1) != 1) 
    {
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
    if (pid == 0) 
    {
        execl("/bin/sh", "sh", AP_MODE_SCRIPT, (char *)NULL);
        _exit(127); // exec failed
    }
    if (pid < 0) 
    {
        perror("fork (AP)");
        return -1;
    }

    ap_pid = pid;
    return 0;
}

static int run_wifi_script(void)
{
    pid_t pid = fork();
    if (pid == 0) 
    {
        execl("/bin/sh", "sh", ENABLE_WIFI_SCRIPT, (char *)NULL);
        _exit(127); // exec failed
    }
    if (pid < 0) 
    {
        perror("fork (WIFI)");
        return -1;
    }

    wifi_pid = pid;
    return 0;
}

static int stop_wifi_script(void)
{
    if (wifi_pid > 0) 
    {
        if (kill(wifi_pid, SIGTERM) == 0) 
        {
            printf("Sent SIGTERM to WIFI script pid=%d\n", wifi_pid);
            wifi_pid = -1;
            return 0;
        } 
        else 
        {
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
    if (!fp) 
    {
        perror("popen (pgrep)");
        return -1;
    }

    char pid_str[16];
    if (fgets(pid_str, sizeof(pid_str), fp) != NULL) 
    {
        pid_t pid = (pid_t)atoi(pid_str);
        printf("Found %s PID: %d\n", APP_NAME, pid);

        if (kill(pid, SIGTERM) == 0) 
        {
            printf("Sent SIGTERM to %s\n", APP_NAME);
        } 
        else 
        {
            perror("kill (APP)");
        }
    } 
    else 
    {
        printf("%s not running\n", APP_NAME);
    }

    pclose(fp);
    return 0;
}

/* ====================== WIFI STATUS / RECONNECT ====================== */

typedef enum 
{
    WIFI_STATE_UNKNOWN = 0,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_COMPLETED
} wifi_state_t;

typedef struct 
{
    wifi_state_t state;
    char ip[64];
} wifi_status_t;


typedef enum {
    WIFI_SM_DISCONNECTED =0,
    WIFI_SM_CONNECTED = 1
} wifi_sm_state_t;

typedef struct {
    wifi_sm_state_t state; // connected / disconnected
    int internet_ok; // 0/1
    char ip[64]; // current IP
} wifi_sm_status_t;

static const char *WIFI_STATE_FILE = "/tmp/wifi_state";
static const char *WIFI_STATE_TMP = "/tmp/wifi_state.tmp";

#define UDHCPC_PIDFILE "/tmp/udhcpc.wlan0.pid"



// ---------- small utils ----------

static void trim_newline(char *s)
{
    if (!s) return;
    for (char *p = s; *p; p++) 
    {
        if (*p == '\n' || *p == '\r') 
        { 
            *p = '\0'; 
            break; 
        }
    }
}

static int run_cmd_capture_rc(const char *cmd, char *out, size_t out_len)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    out[0] = '\0';
    size_t total = 0;

    while (fgets(out + total, (int)(out_len - total), fp)) 
    {
        total = strlen(out);
        if (total >= out_len - 1) break;
    }

    int status = pclose(fp);
    if (status == -1) return -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;
    
    
    return 0;
}

static int pid_alive(pid_t pid)
{
    if (pid <= 1) return 0;
    return (kill(pid, 0) == 0);
}

static int udhcpc_running(void)
{
    FILE *f = fopen(UDHCPC_PIDFILE, "r");
    if (!f) return 0;

    int pid = 0;
    if (fscanf(f, "%d", &pid) != 1)
    {
        fclose(f);
        return 0;
    }
    fclose(f);
    return pid_alive((pid_t)pid);
}


// Real IPv4 + default route + gateway helpers

static int get_wlan0_ipv4(char *ip_out, size_t ip_len)
{
    char buf[256] = {0};

   const char *cmd =
    "PATH=/sbin:/bin:/usr/sbin:/usr/bin:$PATH; "
    "ip -4 -o addr show dev wlan0 | "
    "awk '{split($4,a,\"/\"); print a[1]; exit}'";


    if (run_cmd_capture_rc(cmd, buf, sizeof(buf)) != 0)
        return -1;

    trim_newline(buf);
    if (buf[0] == '\0')
        return -1;

    snprintf(ip_out, ip_len, "%s", buf);
    return 0;
}




static int get_default_gateway(char *gw_out, size_t gw_len)
{
    char buf[256] = {0};
    if (run_cmd_capture_rc("ip route | awk '/default/ {print $3; exit}'", buf, sizeof(buf)) != 0)
        return -1;

    trim_newline(buf);
    if (buf[0] == '\0')
        return -1;

    snprintf(gw_out, gw_len, "%s", buf);
    return 0;
}

static int have_default_route(void)
{
    char gw[64];
    return (get_default_gateway(gw, sizeof(gw)) == 0);
}


static void wifi_run_dhcp_once(void)
{
    if (udhcpc_running()) {
        printf("[WiFi] udhcpc already running, skip DHCP.\n");
        return;
    }

    printf("[WiFi] Running DHCP via udhcpc...\n");

    // Clear stray udhcpc from boot scripts
    system("killall -q udhcpc >/dev/null 2>&1");
    unlink(UDHCPC_PIDFILE);

    system("udhcpc -i wlan0 -p " UDHCPC_PIDFILE " -n -t 3 -T 2 >/dev/console 2>&1");
}


static int internet_check_basic(void)
{
    char gw[64] = {0};

    if (!have_default_route())
        return 0;

    if (get_default_gateway(gw, sizeof(gw)) != 0)
        return 0;

    // ping gateway (usually allowed; fast)
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ping -c 1 -W 1 %s >/dev/null 2>&1", gw);
    if (system(cmd) != 0)
        return 0;

    // Optional: public ping (can be blocked; treat as "nice to have")
    // If you enable this, don't trigger DHCP on failure.
    // if (system("ping -c 1 -W 1 1.1.1.1 >/dev/null 2>&1") != 0) return 0;

    return 1;
}


// ping -c 1 -W 1 192.168.0.1


static int tcp_connect_check(const char *ip, int port, int timeout_ms)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;

    // non-blocking
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        close(fd);
        return 0;
    }

    int r = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (r == 0) { 
        close(fd); 
        return 1; 
    }

    if (errno != EINPROGRESS) { 
        close(fd); 
        return 0; 
    }

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    r = select(fd + 1, NULL, &wfds, NULL, &tv);
    if (r <= 0) { 
        close(fd); 
        return 0; 
    }

    int soerr = 0;
    socklen_t slen = sizeof(soerr);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &slen);

    close(fd);
    return (soerr == 0);
}



#define CLOUD_IP   "139.162.41.206"
#define CLOUD_PORT 22    // or 443 (preferred long-term)

// static int internet_check_basic(void)
// {
//     return tcp_connect_check(CLOUD_IP, CLOUD_PORT, 1000); // 1s timeout
// }


static int write_wifi_state(const wifi_sm_status_t *st)
{
    int fd = open(WIFI_STATE_TMP, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fd < 0)
    {
        perror("[Boot Manager] open wifi_state.tmp failed\n");
        return -1;
    }

    char buf[256];
    int len = snprintf(buf, sizeof(buf),
                        "STATE=%s\nIP=%s\nINTERNET=%d\n",
        (st->state == WIFI_SM_CONNECTED) ? "CONNECTED" : "DISCONNECTED",
        st->ip[0] ? st->ip : "",
        st->internet_ok ? 1 : 0
    );

    ssize_t w = write(fd,  buf, len);

    if(w!=len)
    {
        perror("[Boot Manager] write wifi_state.tmp failed\n");
        close(fd);
        return -1;
    }

    // // Ensure data hits disk
    // if(fsync(fd) < 0)
    // {
    //     perror("[Boot Manager] fsync wifi_state.tmp failed\n");
        
    // }

    close(fd);

    // Atomic rename // reader see either old or new file, never partial file
    if(rename(WIFI_STATE_TMP, WIFI_STATE_FILE) < 0)
    {
        perror("[Boot Manager] rename wifi_state.tmp failed\n");
        return -1;
    }

    return 0;


}


static wifi_state_t map_wpa_state(const char *v)
{
    // Disconnected-like states
    if (!v) return WIFI_STATE_UNKNOWN;

    if (!strcmp(v, "DISCONNECTED") ||
        !strcmp(v, "INACTIVE") ||
        !strcmp(v, "INTERFACE_DISABLED")) {
        return WIFI_STATE_DISCONNECTED;
    }

    if (!strcmp(v, "COMPLETED")) {
        return WIFI_STATE_COMPLETED;
    }

    // Everything else: we assume connecting/handshaking/scanning
    return WIFI_STATE_CONNECTING;
}

static void wifi_get_status(wifi_status_t *st)
{
    memset(st, 0, sizeof(*st));
    st->state = WIFI_STATE_UNKNOWN;

    char buf[2048] = {0};
    if (run_cmd_capture_rc("/system/tools/wifi/wpa_cli -i wlan0 status", buf, sizeof(buf)) != 0) {
        // last resort: try interface IP only
        if (get_wlan0_ipv4(st->ip, sizeof(st->ip)) == 0)
            st->state = WIFI_STATE_COMPLETED;
        return;
    }

    const char *wpa_v = NULL;
    char wpa_ip[64] = {0};

    char *saveptr = NULL;
    char *line = strtok_r(buf, "\n", &saveptr);
    while (line) {
        if (!strncmp(line, "wpa_state=", 10)) {
            wpa_v = line + 10;
        } else if (!strncmp(line, "ip_address=", 11)) {
            snprintf(wpa_ip, sizeof(wpa_ip), "%s", line + 11);
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    st->state = map_wpa_state(wpa_v);

    // Prefer real interface IP
    if (get_wlan0_ipv4(st->ip, sizeof(st->ip)) != 0) {
        // Fallback to wpa_cli ip_address
        if (wpa_ip[0]) snprintf(st->ip, sizeof(st->ip), "%s", wpa_ip);
        else st->ip[0] = '\0';
    }
}



static int wifi_wait_for_completed(int timeout_ms)
{
    const int step_ms = 500;
    int waited = 0;
    wifi_status_t st;

    while (waited < timeout_ms) 
    {
        wifi_get_status(&st);
        if (st.state == WIFI_STATE_COMPLETED) 
        {
            printf("[WiFi] State COMPLETED. ip=%s\n", st.ip[0] ? st.ip : "(none)");

            // If still no IP, try DHCP once
            if (st.ip[0] == '\0')
                wifi_run_dhcp_once();

            return 0;
        }
        usleep(step_ms * 1000);
        waited += step_ms;
    }
    return -1;
}

static void wifi_soft_reconnect(void)
{
    printf("[WiFi] Sending wpa_cli reconnect...\n");
    system("/system/tools/wifi/wpa_cli -i wlan0 reconnect >/dev/console 2>&1");
}


/* ====================== WIFI WATCHDOG THREAD ====================== */



static void *wifi_watchdog_thread(void *arg)
{
    (void)arg;

    wifi_status_t st;
    wifi_state_t last_state = WIFI_STATE_UNKNOWN;

    int disconnected_count = 0;
    int connecting_count   = 0;
    int internet_fail_count = 0;

    int backoff_sec = 10;          // start
    const int backoff_max = 60;    // max backoff

    wifi_sm_status_t sm = {0};

    while (1) 
    {
        sleep(backoff_sec);

        if (ap_mode_active) 
        {
            sm.state = WIFI_SM_DISCONNECTED;
            sm.internet_ok = 0;
            sm.ip[0] = '\0';
            write_wifi_state(&sm);

            // reset counters while in AP mode
            disconnected_count = connecting_count = internet_fail_count = 0;
            backoff_sec = 10;
            last_state = WIFI_STATE_UNKNOWN;
            continue;
        }

        wifi_get_status(&st);

        if (st.state != last_state) 
        {
            printf("[WiFi] State changed: %d -> %d, ip=%s\n", last_state, st.state, st.ip[0] ? st.ip : "(none)");
            last_state = st.state;
        }

        // Default publish
        sm.state = WIFI_SM_DISCONNECTED;
        sm.internet_ok = 0;
        sm.ip[0] = '\0';

        // ---------- COMPLETED ----------
        if (st.state == WIFI_STATE_COMPLETED) 
        {
            sm.state = WIFI_SM_CONNECTED;
            if (st.ip[0]) 
            {
                snprintf(sm.ip, sizeof(sm.ip), "%s", st.ip);
            }

            // If COMPLETED but no IP => DHCP once
            if (st.ip[0] == '\0') 
            {
                printf("[WiFi] COMPLETED but no IP. DHCP once.\n");
                wifi_run_dhcp_once();
            }

            // Internet check (don’t DHCP on fail)
            sm.internet_ok = internet_check_basic();
            if (!sm.internet_ok) 
            {
                internet_fail_count++;
                printf("[WiFi] Internet check failed (%d).\n", internet_fail_count);

                // Escalate only after several consecutive failures
                if (internet_fail_count >= 6) 
                { 
                    // ~6 loops = 60s if backoff 10
                    printf("[WiFi] Internet failing for a while. Trigger reconnect.\n");
                    wifi_soft_reconnect();
                    wifi_wait_for_completed(10000);
                    internet_fail_count = 0;
                }
            } 
            else 
            {
                internet_fail_count = 0;
            }

            write_wifi_state(&sm);

            // Healthy => reset backoff/counters
            disconnected_count = 0;
            connecting_count = 0;
            backoff_sec = 10;
            continue;
        }

        // ---------- CONNECTING ----------
        if (st.state == WIFI_STATE_CONNECTING) 
        {
            connecting_count++;
            write_wifi_state(&sm);

            // Give it time to finish handshake; don't spam reconnect
            if (connecting_count >= 6) 
            { 
                // ~60s at 10s interval
                printf("[WiFi] Stuck CONNECTING too long. Reconnect.\n");
                wifi_soft_reconnect();
                wifi_wait_for_completed(10000);
                connecting_count = 0;
            }

            // gentle backoff while not ready
            // if (backoff_sec < backoff_max) backoff_sec += 10;

            backoff_sec = 10;   // fixed while connecting

            continue;
        }

        // ---------- DISCONNECTED / UNKNOWN ----------
        disconnected_count++;
        connecting_count = 0;
        write_wifi_state(&sm);

        printf("[WiFi] Disconnected/Unknown (state=%d). Attempt reconnect #%d\n",
               st.state, disconnected_count);

        // Reconnect only after a couple confirmations (avoid transient glitches)
        if (disconnected_count >= 2) 
        {
            wifi_soft_reconnect();
            if (wifi_wait_for_completed(10000) == 0) 
            {
                printf("[WiFi] Reconnected successfully.\n");
                disconnected_count = 0;
                backoff_sec = 10;
            } 
            else 
            {
                printf("[WiFi] Reconnect timed out.\n");
                if (backoff_sec < backoff_max) backoff_sec += 10;
            }
        } else 
        {
            // first time disconnected: wait another loop
            if (backoff_sec < backoff_max) backoff_sec += 10;
        }
    }

    return NULL;
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

        if (ret > 0 && (pfd.revents & POLLPRI)) 
        {
            // Debounce / long press check
            sleep(2);

            if (read_gpio_value() == 0) 
            {
                printf("[BOOT BTN] Long press detected. Stop WIFI + APP, launch AP mode...\n");
                stop_wifi_script();
                stop_application();
                run_ap_script();
                ap_mode_active = 1;
            } 
            else 
            {
                printf("[BOOT BTN] Short press ignored.\n");
            }
        }
    }

    close(fd);
    unexport_gpio();
    return NULL;
}



/* =============================== tiny server =============================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>

// #ifndef DEBUG
// #define DEBUG 1
// #endif

// #if DEBUG
//   #define LOG(fmt, ...) fprintf(stderr, "[tiny] " fmt "\n", ##__VA_ARGS__)
// #else
//   #define LOG(fmt, ...) do{}while(0)
// #endif

/* ------ config ------ */
#ifndef PORT
#define PORT        8080
#endif
#define RECV_BUFSZ  163840
#define BODY_BUFSZ  163840
#define STR_MAX     256

#ifndef CFG_TXT_PATH
#define CFG_TXT_PATH "/system/etc/device_wifi_config.txt"
#endif

#ifndef ENABLE_WPA_WRITE
#define ENABLE_WPA_WRITE 0   /* set to 1 to write /etc/wpa_supplicant.conf */
#endif
#define WPA_PATH     "/etc/wpa_supplicant.conf"
#define IFACE        "wlan0"

/* ------ helpers ------ */

/* mask secret: keep first/last 2 chars if long, else **** */
static void mask_secret(const char *in, char *out, size_t outsz)
{
    size_t n = in ? strlen(in) : 0;
    if (!in || !n){ snprintf(out,outsz,"<empty>"); return; }
    if (n <= 4){ snprintf(out,outsz,"****"); return; }
    snprintf(out,outsz,"%.*s****%.*s", 2, in, 2, in + (int)n - 2);
}

/* case-insensitive strstr */
static const char* ci_strstr(const char *haystack, const char *needle)
{
    if(!haystack || !needle) return NULL;
    size_t nlen = strlen(needle);
    if(nlen == 0) return haystack;
    for (const char *p = haystack; *p; p++){
        size_t i = 0;
        while (i < nlen &&
               p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == nlen) return p;
    }
    return NULL;
}

static int safe_write(const char *path, const char *content, mode_t mode)
{
    char tmp[STR_MAX];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if(!f)
    {
        printf("safe_write: fopen(%s) failed: %s", tmp, strerror(errno));
        return 0;
    }
    if (fputs(content, f) == EOF)
    {
        printf("safe_write: fputs to %s failed: %s", tmp, strerror(errno));
        fclose(f);
        unlink(tmp);
        return 0;
    }
    if (fclose(f) != 0)
    {
        printf("safe_write: fclose(%s) failed: %s", tmp, strerror(errno));
        unlink(tmp);
        return 0;
    }
    if (chmod(tmp, mode) != 0)
    {
        printf("safe_write: chmod(%s, 0%o) failed: %s", tmp, (unsigned)mode, strerror(errno));
        unlink(tmp);
        return 0;
    }
    if (rename(tmp, path) != 0)
    {
        printf("safe_write: rename(%s -> %s) failed: %s", tmp, path, strerror(errno));
        unlink(tmp);
        return 0;
    }
    printf("safe_write: wrote %s (%zu bytes)", path, strlen(content));
    return 1;
}

/* tiny JSON string extractor: finds "key":"value" */
static int json_get_str(const char *json, const char *key, char *out, size_t outsz)
{
    if (!json || !key || !out || outsz < 2) return 0;
    char pattern[STR_MAX];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *k = strstr(json, pattern);
    if (!k) return 0;
    const char *p = strchr(k + strlen(pattern), ':');
    if (!p) return 0;
    for (p++; *p && isspace((unsigned char)*p); p++);
    if (*p != '\"') return 0;
    p++; // after opening quote
    size_t i = 0;
    while (*p && *p != '\"' && i < outsz-1) 
    {
        if (*p == '\\' && *(p+1)) p++; // skip escape simply
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

/* JSON quote helper: writes "str" or null */
static void json_quote_or_null(char *out, size_t outsz, const char *str)
{
    if (!str || !str[0]) { snprintf(out, outsz, "null"); return; }
    char buf[STR_MAX]; size_t j = 0;
    for (size_t i=0; str[i] && j < sizeof(buf)-2; i++)
    {
        char c = str[i];
        if (c == '\"' || c == '\\') { if (j < sizeof(buf)-2) buf[j++]='\\'; }
        buf[j++] = c;
    }
    buf[j] = '\0';
    snprintf(out, outsz, "\"%s\"", buf);
}

/* Read IPv4 via ioctl; returns 1 if found */
static int get_iface_ip(char ipbuf[STR_MAX])
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0){ printf("socket(AF_INET,SOCK_DGRAM) failed: %s", strerror(errno)); return 0; }
    struct ifreq ifr; memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, IFACE, IFNAMSIZ-1);
    if (ioctl(fd, SIOCGIFADDR, &ifr) == 0)
    {
        struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
        const char *ip = inet_ntoa(sin->sin_addr);
        if (ip && strcmp(ip, "0.0.0.0") != 0)
        {
            strncpy(ipbuf, ip, STR_MAX-1);
            close(fd);
            return 1;
        }
    } 
    else 
    {
        printf("ioctl(SIOCGIFADDR) on %s failed: %s", IFACE, strerror(errno));
    }
    close(fd);
    return 0;
}

/* Determine mode */
static const char* detect_mode(char ip_out[STR_MAX])
{
    ip_out[0] = '\0';
    if (access("/var/run/hostapd/hostapd.pid", F_OK) == 0) 
    {
        (void)get_iface_ip(ip_out);
        printf("detect_mode: hostapd pid present, ip=%s", ip_out[0]?ip_out:"(none)");
        return "AP";
    }
    if (get_iface_ip(ip_out)) 
    {
        const char *m = (strcmp(ip_out, "192.168.4.1") == 0) ? "AP" : "STA";
        printf("detect_mode: iface ip=%s -> %s", ip_out, m);
        return m;
    }
    printf("detect_mode: UNKNOWN (no IP)");
    return "UNKNOWN";
}

/* Persist configs to file + (optional) wpa_supplicant */
static int persist_all(const char *ssid, const char *wifi_password,
                       const char *username, const char *user_password,
                       const char *camera_id){
    /* make sure dir exists */
    system("mkdir -p /system/etc >/dev/null 2>&1");

    char txt[1024];
    snprintf(txt, sizeof(txt),
        "ssid=%s\n"
        "wifi_psk=%s\n"
        "username=%s\n"
        "password=%s\n"
        "camera_id=%s\n"
        "is_reg=1\n"
        "sensor=imx327\n"
        "vendor=teton\n",
        ssid, wifi_password, username, user_password, camera_id);

    printf("persist_all: writing config to %s", CFG_TXT_PATH);
    if (!safe_write(CFG_TXT_PATH, txt, 0600)) 
    {
        printf("persist_all: failed writing %s", CFG_TXT_PATH);
        return 0;
    }

#if ENABLE_WPA_WRITE
    /* /etc/wpa_supplicant.conf (uses ssid + wifi_password) */
    char wpa[1024];
    snprintf(wpa, sizeof(wpa),
        "ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev\n"
        "update_config=1\n"
        "country=BD\n\n"
        "network={\n"
        "    ssid=\"%s\"\n"
        "    psk=\"%s\"\n"
        "    key_mgmt=WPA-PSK\n"
        "}\n",
        ssid, wifi_password);
    printf("persist_all: writing WPA file %s", WPA_PATH);
    if (!safe_write(WPA_PATH, wpa, 0600)) 
    {
        printf("persist_all: failed writing %s", WPA_PATH);
        return 0;
    }
#else
    printf("persist_all: WPA write disabled (ENABLE_WPA_WRITE=0)");
#endif

    return 1;
}

/* send JSON response with code */
static void send_json(int client, int code, const char *json)
{
    char header[256];
    snprintf(header, sizeof(header),
        "HTTP/1.1 %d\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n",
        code, json ? strlen(json) : 0);
    send(client, header, strlen(header), 0);
    if (json) send(client, json, strlen(json), 0);
}

/* ---------- POST /onboard ---------- */
static void handle_onboard(int client, const char *body)
{
    char ssid[STR_MAX]="", wifi_password[STR_MAX]="", username[STR_MAX]="", user_password[STR_MAX]="", camera_id[STR_MAX]="";
    printf("handle_onboard: body len ~%zu", strlen(body));

    if (!json_get_str(body, "ssid", ssid, sizeof(ssid)) ||
        !json_get_str(body, "wifi_password", wifi_password, sizeof(wifi_password))) 
    {
        printf("handle_onboard: missing ssid or wifi_password");
        send_json(client, 400, "{\"ok\":false,\"error\":\"missing ssid or wifi_password\"}\n");
        return;
    }
    json_get_str(body, "username",  username,      sizeof(username));
    json_get_str(body, "password",  user_password, sizeof(user_password));
    json_get_str(body, "camera_id", camera_id,     sizeof(camera_id));

    char wifi_pw_mask[64], user_pw_mask[64];
    mask_secret(wifi_password, wifi_pw_mask, sizeof(wifi_pw_mask));
    mask_secret(user_password, user_pw_mask, sizeof(user_pw_mask));
    printf("handle_onboard: ssid='%s', wifi_password='%s', username='%s', password='%s', camera_id='%s'",
        ssid, wifi_pw_mask, username[0]?username:"(none)", user_pw_mask, camera_id[0]?camera_id:"(none)");

    if (!persist_all(ssid, wifi_password, username, user_password, camera_id))
    {
        printf("handle_onboard: persist_all FAILED");
        send_json(client, 500, "{\"ok\":false,\"error\":\"persist failed\"}\n");
        return;
    }

    

    send_json(client, 200, "{\"ok\":true,\"msg\":\"saved; switching to STA\"}\n");

    printf("************\n\n get wifi credential\n");
    ap_mode_active = 0;


   pid_t pid = fork();
    if (pid == 0) 
    {
        printf("handle_onboard: exec /system/www/enable_wifi.sh");
        execl("/bin/sh", "sh", "/system/www/enable_wifi.sh", (char*)NULL);
        printf("handle_onboard: exec failed: %s", strerror(errno));
        _exit(0);
    } 
    else if (pid > 0) 
    {
        printf("handle_onboard: spawned enable_wifi.sh pid=%d", (int)pid);
    } 
    else 
    {
        printf("handle_onboard: fork failed: %s", strerror(errno));
    }

}

/* parse key=value lines from CFG_TXT_PATH */
static void load_cfg(char *ssid, char *username, char *camera_id, int *has_wifi_psk, int *has_user_pass)
{
    FILE *f = fopen(CFG_TXT_PATH, "r");
    ssid[0]=username[0]=camera_id[0]='\0';
    if (has_wifi_psk) *has_wifi_psk = 0;
    if (has_user_pass) *has_user_pass = 0;
    if (!f)
    {
        printf("load_cfg: fopen(%s) failed: %s", CFG_TXT_PATH, strerror(errno));
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = '\0';
        char *k = line, *v = eq+1;
        v[strcspn(v, "\r\n")] = '\0';
        if (strcmp(k, "ssid")==0)            strncpy(ssid, v, STR_MAX-1);
        else if (strcmp(k, "username")==0)   strncpy(username, v, STR_MAX-1);
        else if (strcmp(k, "camera_id")==0)  strncpy(camera_id, v, STR_MAX-1);
        else if (strcmp(k, "wifi_psk")==0 && has_wifi_psk) *has_wifi_psk = (v[0] != '\0');
        else if (strcmp(k, "password")==0 && has_user_pass) *has_user_pass = (v[0] != '\0');
    }
    fclose(f);
    printf("load_cfg: ssid='%s', username='%s', camera_id='%s', has_wifi_psk=%d, has_user_pass=%d",
        ssid[0]?ssid:"(none)", username[0]?username:"(none)", camera_id[0]?camera_id:"(none)",
        has_wifi_psk?*has_wifi_psk:0, has_user_pass?*has_user_pass:0);
}

/* ---------- GET /status ---------- */
static void handle_status(int client){
    char ip[STR_MAX]; const char *mode = detect_mode(ip);

    char ssid[STR_MAX], username[STR_MAX], camera_id[STR_MAX];
    int has_wifi_psk = 0, has_user_pass = 0;
    load_cfg(ssid, username, camera_id, &has_wifi_psk, &has_user_pass);

    char ipq[STR_MAX+2];      json_quote_or_null(ipq, sizeof(ipq), ip[0]?ip:NULL);
    char ssidq[STR_MAX+2];    json_quote_or_null(ssidq, sizeof(ssidq), ssid[0]?ssid:NULL);
    char userq[STR_MAX+2];    json_quote_or_null(userq, sizeof(userq), username[0]?username:NULL);
    char camidq[STR_MAX+2];   json_quote_or_null(camidq, sizeof(camidq), camera_id[0]?camera_id:NULL);

    char json[1024];
    snprintf(json, sizeof(json),
        "{"
          "\"ok\":true,"
          "\"mode\":\"%s\","
          "\"ip\":%s,"
          "\"config\":{"
            "\"ssid\":%s,"
            "\"username\":%s,"
            "\"camera_id\":%s,"
            "\"has_wifi_psk\":%s,"
            "\"has_user_password\":%s"
          "}"
        "}\n",
        mode, ipq, ssidq, userq, camidq,
        has_wifi_psk ? "true":"false",
        has_user_pass ? "true":"false");

    printf("handle_status: mode=%s ip=%s", mode, ip[0]?ip:"(none)");
    send_json(client, 200, json);
}


static void *tiny_server_thread(void *arg)
{
    int sockfd;

    struct sockaddr_in serv;
    int one = 1;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    { 
        perror("socket"); 
        // return 1; 
    }

    // int one = 1; 
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    // struct sockaddr_in serv = {0};

    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr*)&serv, sizeof(serv)) < 0)
    {
        perror("bind");
        printf("bind failed on port %d. Is something else listening?", PORT);
        // return 1;
    }

    if (listen(sockfd, 8) < 0)
    { 
        perror("listen"); 
        printf("listening error");
        // return 1; 
    }


    printf("tiny_onboard_server listening on 0.0.0.0:%d\n", PORT);

    while(1)
    {
        struct sockaddr_in cli; socklen_t clilen = sizeof(cli);
        int cfd = accept(sockfd, (struct sockaddr*)&cli, &clilen);
        if (cfd < 0) { printf("accept failed: %s", strerror(errno)); continue; }

        printf("client: %s:%d connected", inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));

        char req[RECV_BUFSZ]; int n = recv(cfd, req, sizeof(req)-1, 0);
        if (n <= 0){ printf("recv <=0 (%d), closing", n); close(cfd); continue; }
        req[n] = '\0';

        /* printf first line */
        char *eol = strstr(req, "\r\n");
        if (eol){ *eol = '\0'; printf("request-line: %s", req); *eol = '\r'; }
        else     { printf("request-chunk: %.80s", req); }

        int is_post_onboard = 0, is_get_status = 0;
        if (strncmp(req, "POST /onboard ", 14) == 0 || strstr(req, "POST /onboard "))
            is_post_onboard = 1;
        else if (strncmp(req, "GET /status ", 12) == 0 || strstr(req, "GET /status "))
            is_get_status = 1;

        if (!is_post_onboard && !is_get_status) {
            printf("no matching endpoint");
            send_json(cfd, 404, "{\"ok\":false,\"error\":\"not found\"}\n");
            close(cfd);
            continue;
        }

        if (is_get_status)
        {
            handle_status(cfd);
            close(cfd);
            while (waitpid(-1, NULL, WNOHANG) > 0) {}
            continue;
        }

        /* POST /onboard body */
        int content_len = 0;
        const char *cl = ci_strstr(req, "Content-Length:");
        if (cl){
            cl += strlen("Content-Length:");
            while (*cl && (*cl==':' || isspace((unsigned char)*cl))) cl++;
            content_len = atoi(cl);
        }
        printf("Content-Length: %d", content_len);
        if (content_len <= 0 || content_len >= BODY_BUFSZ){
            printf("invalid Content-Length");
            send_json(cfd, 400, "{\"ok\":false,\"error\":\"invalid Content-Length\"}\n");
            close(cfd);
            continue;
        }

        const char *sep = strstr(req, "\r\n\r\n");
        char body[BODY_BUFSZ]; int body_read = 0;
        if (sep){
            sep += 4;
            int already = (int)((req + n) - sep);
            if (already > 0){
                if (already > content_len) already = content_len;
                memcpy(body, sep, already);
                body_read = already;
            }
        }
        while (body_read < content_len) 
        {
            int r = recv(cfd, body + body_read, content_len - body_read, 0);
            if (r <= 0) break;
            body_read += r;
            if (body_read >= (BODY_BUFSZ - 1)) break;
        }
        
        body[(body_read < BODY_BUFSZ-1) ? body_read : (BODY_BUFSZ-1)] = '\0';
        printf("body_read: %d bytes", body_read);

        handle_onboard(cfd, body);
        close(cfd);

        /* reap any children (switch script) */
        while (waitpid(-1, NULL, WNOHANG) > 0) {}
    }

    close(sockfd);
    return 0;
}





/* =============================== MAIN =============================== */

int main(void)
{
    pthread_t tid_btn, tid_wifi, tid_tiny_server;

    if (pthread_create(&tid_btn, NULL, button_thread, NULL) != 0) 
    {
        perror("pthread_create button_thread");
    }

    // Start initial WiFi / application script
    if (run_wifi_script() != 0) 
    {
        fprintf(stderr, "Failed to start WIFI script\n");
    }



    if (pthread_create(&tid_wifi, NULL, wifi_watchdog_thread, NULL) != 0) 
    {
        perror("pthread_create wifi_watchdog_thread");
    }

    if (pthread_create(&tid_tiny_server, NULL, tiny_server_thread, NULL) != 0) 
    {
        perror("tiny server thread error problem");
    }

    pthread_join(tid_btn, NULL);
    pthread_join(tid_wifi, NULL);
    pthread_join(tid_tiny_server, NULL);

    return 0;
}
