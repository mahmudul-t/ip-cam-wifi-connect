# Project: Button-Driven Wi‑Fi/AP Orchestrator (single binary with built-in Tiny Server)
# -----------------------------------------------------------------------------
# Layout
#   Makefile
#   src/
#     config.h
#     log.h
#     gpio_btn.h        gpio_btn.c
#     proc.h            proc.c
#     net.h             net.c
#     tinyserver.h      tinyserver.c   <-- integrated tiny server (from your code)
#     flow.h            flow.c         <-- starts/stops tiny server via API
#     orchestrator_main.c
#
# Build:
#   $ make            # native
#   $ make STATIC=1   # static (BusyBox-friendly)
#
# Runtime:
#   - Binary is a single daemon: `orchestrator`
#   - On long press: kills wifi/app -> starts AP -> **starts embedded tiny server** -> waits
#     for onboard POST -> Wi‑Fi (enable_wifi.sh) -> NFS -> app.

// ========================= Makefile =========================
TARGET := orchestrator
SRCS := \
  src/orchestrator_main.c \
  src/gpio_btn.c \
  src/proc.c \
  src/net.c \
  src/tinyserver.c \
  src/flow.c
OBJS := $(SRCS:.c=.o)
CFLAGS := -O2 -Wall -Wextra -I./src
LDFLAGS :=

ifdef STATIC
  LDFLAGS += -static
endif

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: clean


// ========================= src/config.h =========================
#ifndef CONFIG_H
#define CONFIG_H

// ---- Paths (adjust as needed) ----
#define IFACE               "wlan0"
#define GPIO_NUM_STR        "64"
#define GPIO_DIRPATH        "/sys/class/gpio/gpio64/"  // matches GPIO_NUM_STR

#define WIFI_SH             "/system/www/enable_wifi.sh"
#define AP_MODE_SH          "/system/www/ap_mode_enable.sh"
#define NFS_SH              "/system/start_nfs.sh"
#define APP_BIN             "/system/nfs/keo-cam"

// ---- Tiny server (built-in) ----
#define TINY_PORT           8080
#define CFG_TXT_PATH        "/system/etc/device_wifi_config.txt"
#define ENABLE_WPA_WRITE    0        // set to 1 to write WPA file
#define WPA_PATH            "/etc/wpa_supplicant.conf"

// ---- Logging files ----
#define LOG_WIFI_PATH       "/tmp/orch_wifi.log"
#define LOG_AP_PATH         "/tmp/orch_ap.log"
#define LOG_TINY_PATH       "/tmp/orch_tiny.log"   // orchestrator logs tinyserver events too
#define LOG_NFS_PATH        "/tmp/orch_nfs.log"
#define LOG_APP_PATH        "/tmp/orch_app.log"

// ---- Timeouts / timings (ms) ----
#define BUTTON_LONG_MS      2000
#define RETRIGGER_GUARD_S   20
#define TIMEOUT_LISTEN_MS   15000
#define TIMEOUT_STA_MS      90000
#define TIMEOUT_NFS_MS      20000
#define TERM_GRACE_MS       300

// AP subnet prefix considered "AP mode" (change if different)
#define AP_SUBNET_PREFIX    "192.168.4."

#endif // CONFIG_H


// ========================= src/log.h =========================
#ifndef LOG_H
#define LOG_H
#include <stdio.h>
#include <stdarg.h>
static inline void logf_tag(const char *tag, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[%s] ", tag);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "
");
    va_end(ap);
}
#endif // LOG_H


// ========================= src/gpio_btn.h =========================
#ifndef GPIO_BTN_H
#define GPIO_BTN_H
int gpio_init(void);
int gpio_wait_long_press(void); // returns 1 on long press, 0 otherwise
#endif


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


// ========================= src/proc.h =========================
#ifndef PROC_H
#define PROC_H
#include <sys/types.h>

typedef struct { pid_t pid; pid_t pgid; } child_t;

int  spawn_sh(const char *script_path, const char *logfile, child_t *out);
int  spawn_exec(const char *path, char *const argv[], const char *logfile, child_t *out);
void kill_group(pid_t pgid, const char *what);
void pkill_like(const char *needle, const char *tag);

#endif


// ========================= src/proc.c =========================
#define _XOPEN_SOURCE 700
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include "config.h"
#include "log.h"
#include "proc.h"

static int redirect_logs(const char *logfile) {
    if (!logfile || !*logfile) return 0;
    int fd = open(logfile, O_WRONLY|O_CREAT|O_APPEND, 0644);
    if (fd < 0) return -1;
    if (dup2(fd, STDOUT_FILENO) < 0) { close(fd); return -1; }
    if (dup2(fd, STDERR_FILENO) < 0) { close(fd); return -1; }
    if (fd > 2) close(fd);
    return 0;
}

int spawn_sh(const char *script_path, const char *logfile, child_t *out) {
    pid_t pid = fork();
    if (pid < 0) { logf_tag("proc", "fork failed: %s", strerror(errno)); return -1; }
    if (pid == 0) {
        setpgid(0, 0);
        (void)redirect_logs(logfile);
        execl("/bin/sh", "sh", script_path, (char*)NULL);
        fprintf(stderr, "exec sh '%s' failed: %s
", script_path, strerror(errno));
        _exit(127);
    }
    setpgid(pid, pid);
    if (out) { out->pid = pid; out->pgid = pid; }
    return 0;
}

int spawn_exec(const char *path, char *const argv[], const char *logfile, child_t *out) {
    pid_t pid = fork();
    if (pid < 0) { logf_tag("proc", "fork failed: %s", strerror(errno)); return -1; }
    if (pid == 0) {
        setpgid(0, 0);
        (void)redirect_logs(logfile);
        if (argv) execvp(path, argv); else execl(path, path, (char*)NULL);
        fprintf(stderr, "exec '%s' failed: %s
", path, strerror(errno));
        _exit(127);
    }
    setpgid(pid, pid);
    if (out) { out->pid = pid; out->pgid = pid; }
    return 0;
}

void kill_group(pid_t pgid, const char *what) {
    if (pgid <= 0) return;
    kill(-pgid, SIGTERM);
    usleep(TERM_GRACE_MS * 1000);
    if (kill(-pgid, 0) == 0) {
        kill(-pgid, SIGKILL);
        logf_tag("kill", "forced KILL %s (pgid=%d)", what, (int)pgid);
    } else {
        logf_tag("kill", "TERM ok for %s (pgid=%d)", what, (int)pgid);
    }
}

void pkill_like(const char *needle, const char *tag) {
    DIR *d = opendir("/proc");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        pid_t pid = atoi(e->d_name);
        if (pid <= 1) continue;
        char path[128]; snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        int fd = open(path, O_RDONLY); if (fd < 0) continue;
        char buf[512]; int n = read(fd, buf, sizeof(buf)-1); close(fd);
        if (n <= 0) continue; buf[n] = ' ';
        if (strstr(buf, needle)) {
            pid_t pg = getpgid(pid);
            if (pg > 0) kill_group(pg, needle);
            else { kill(pid, SIGTERM); usleep(TERM_GRACE_MS*1000); kill(pid, SIGKILL); }
            logf_tag(tag, "pkill_like killed pid=%d (needle=%s)", (int)pid, needle);
        }
    }
    closedir(d);
}


// ========================= src/net.h =========================
#ifndef NET_H
#define NET_H
int wait_port_listen(int port, int timeout_ms);
int wait_sta_ip(int timeout_ms);
int wait_mountpoint(const char *mp, int timeout_ms);
#endif


// ========================= src/net.c =========================
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <stdio.h>
#include "config.h"
#include "log.h"

static int connect_local_port(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int r = connect(s, (struct sockaddr*)&a, sizeof(a));
    close(s); return r;
}

int wait_port_listen(int port, int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (connect_local_port(port) == 0) return 1;
        usleep(100*1000); elapsed += 100;
    }
    return 0;
}

static int get_iface_ip(char out[64]) {
    out[0] = ' ';
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
    struct ifreq ifr; memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, IFACE, IFNAMSIZ-1);
    if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
        const char *ip = inet_ntoa(sin->sin_addr);
        if (ip) strncpy(out, ip, 63);
        close(fd);
        return (ip && strcmp(ip, "0.0.0.0") != 0);
    }
    close(fd); return 0;
}

static int ip_is_ap_net(const char *ip) {
    return (ip && strncmp(ip, AP_SUBNET_PREFIX, strlen(AP_SUBNET_PREFIX)) == 0);
}

int wait_sta_ip(int timeout_ms) {
    int elapsed = 0; char ip[64];
    while (elapsed < timeout_ms) {
        if (get_iface_ip(ip)) {
            if (!ip_is_ap_net(ip)) { logf_tag("net", "STA IP: %s", ip); return 1; }
        }
        usleep(200*1000); elapsed += 200;
    }
    return 0;
}

static int is_mountpoint(const char *mp) {
    FILE *f = fopen("/proc/mounts", "r"); if (!f) return 0;
    char line[512]; int ok = 0;
    while (fgets(line, sizeof(line), f)) {
        char src[128], mpath[256], fs[64], opts[128];
        if (sscanf(line, "%127s %255s %63s %127s", src, mpath, fs, opts) == 4) {
            if (strcmp(mpath, mp) == 0) { ok = 1; break; }
        }
    }
    fclose(f); return ok;
}

int wait_mountpoint(const char *mp, int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (is_mountpoint(mp)) return 1;
        usleep(200*1000); elapsed += 200;
    }
    return 0;
}


// ========================= src/tinyserver.h =========================
#ifndef TINY_SERVER_H
#define TINY_SERVER_H
#include <stdatomic.h>

// Start the embedded tiny server in a background thread.
// Sets *server_started to 1 when listening; returns 0 on success.
int tiny_server_start(atomic_int *server_started);
// Ask the server to stop and join the thread (safe to call even if not started).
void tiny_server_stop(void);

#endif


// ========================= src/tinyserver.c =========================
// Embedded version of your tiny_onboard_server.c
#define _GNU_SOURCE
#include <pthread.h>
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
#include <stdatomic.h>

#include "config.h"
#include "log.h"

static pthread_t g_thr; 
static atomic_int g_run = 0;

// --- utils from your server ---
static void mask_secret(const char *in, char *out, size_t outsz){
    size_t n = in ? strlen(in) : 0;
    if (!in || !n){ snprintf(out,outsz,"<empty>"); return; }
    if (n <= 4){ snprintf(out,outsz,"****"); return; }
    snprintf(out,outsz,"%.*s****%.*s", 2, in, 2, in + (int)n - 2);
}

static const char* ci_strstr(const char *h, const char *n){
    if(!h||!n) return NULL; size_t L=strlen(n); if(!L) return h;
    for(const char *p=h; *p; ++p){ size_t i=0; while(i<L && p[i] && tolower((unsigned char)p[i])==tolower((unsigned char)n[i])) i++; if(i==L) return p; }
    return NULL;
}

static int safe_write_file(const char *path, const char *content, mode_t mode){
    char tmp[256]; snprintf(tmp,sizeof(tmp),"%s.tmp",path);
    FILE *f=fopen(tmp,"w"); if(!f){ return 0; }
    if(fputs(content,f)==EOF){ fclose(f); unlink(tmp); return 0; }
    if(fclose(f)!=0){ unlink(tmp); return 0; }
    if(chmod(tmp,mode)!=0){ unlink(tmp); return 0; }
    if(rename(tmp,path)!=0){ unlink(tmp); return 0; }
    return 1;
}

static int json_get_str(const char *json, const char *key, char *out, size_t outsz){
    if (!json || !key || !out || outsz < 2) return 0;
    char pattern[128]; snprintf(pattern,sizeof(pattern),"\"%s\"",key);
    const char *k = strstr(json, pattern); if(!k) return 0;
    const char *p = strchr(k + strlen(pattern), ':'); if(!p) return 0;
    for (p++; *p && isspace((unsigned char)*p); p++);
    if (*p != '"') return 0; p++;
    size_t i=0; while(*p && *p!='"' && i<outsz-1){ if(*p=='\' && *(p+1)) p++; out[i++]=*p++; }
    out[i]=' '; return 1;
}

static void json_quote_or_null(char *out, size_t outsz, const char *str){
    if(!str||!str[0]){ snprintf(out,outsz,"null"); return; }
    char buf[256]; size_t j=0; for(size_t i=0; str[i] && j<sizeof(buf)-2; i++){ char c=str[i]; if(c=='"'||c=='\'){ if(j<sizeof(buf)-2) buf[j++]='\'; } buf[j++]=c; } buf[j]=' ';
    snprintf(out,outsz,"\"%s\"",buf);
}

static int get_iface_ip(char ipbuf[64]){
    int fd = socket(AF_INET, SOCK_DGRAM, 0); if(fd<0) return 0;
    struct ifreq ifr; memset(&ifr,0,sizeof(ifr)); strncpy(ifr.ifr_name, IFACE, IFNAMSIZ-1);
    if(ioctl(fd,SIOCGIFADDR,&ifr)==0){ struct sockaddr_in *sin=(struct sockaddr_in *)&ifr.ifr_addr; const char *ip=inet_ntoa(sin->sin_addr); if(ip && strcmp(ip,"0.0.0.0")!=0){ strncpy(ipbuf,ip,63); close(fd); return 1; } }
    close(fd); return 0;
}

static const char* detect_mode(char ip_out[64]){
    ip_out[0]=' ';
    if (access("/var/run/hostapd/hostapd.pid", F_OK) == 0) { (void)get_iface_ip(ip_out); return "AP"; }
    if (get_iface_ip(ip_out)) { return strncmp(ip_out, AP_SUBNET_PREFIX, strlen(AP_SUBNET_PREFIX))==0 ? "AP" : "STA"; }
    return "UNKNOWN";
}

static int persist_all(const char *ssid, const char *wifi_password,
                       const char *username, const char *user_password,
                       const char *camera_id){
    system("mkdir -p /system/etc >/dev/null 2>&1");
    char txt[1024];
    snprintf(txt,sizeof(txt),
        "ssid=%s
wifi_psk=%s
username=%s
password=%s
camera_id=%s
is_reg=1
sensor=imx327
vendor=teton
",
        ssid,wifi_password,username,user_password,camera_id);
    if(!safe_write_file(CFG_TXT_PATH, txt, 0600)) return 0;
#if ENABLE_WPA_WRITE
    char wpa[1024];
    snprintf(wpa,sizeof(wpa),
        "ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev
update_config=1
country=BD

network={
    ssid=\"%s\"
    psk=\"%s\"
    key_mgmt=WPA-PSK
}
",
        ssid,wifi_password);
    if(!safe_write_file(WPA_PATH, wpa, 0600)) return 0;
#endif
    return 1;
}

static void send_json(int client, int code, const char *json){
    char header[256];
    snprintf(header,sizeof(header),"HTTP/1.1 %d
Content-Type: application/json
Content-Length: %zu

", code, json?strlen(json):0);
    send(client, header, strlen(header), 0);
    if(json) send(client, json, strlen(json), 0);
}

static void handle_onboard(int client, const char *body){
    char ssid[256]="", wifi_password[256]="", username[256]="", user_password[256]="", camera_id[256]="";
    if (!json_get_str(body, "ssid", ssid, sizeof(ssid)) || !json_get_str(body, "wifi_password", wifi_password, sizeof(wifi_password))) {
        send_json(client, 400, "{\"ok\":false,\"error\":\"missing ssid or wifi_password\"}
");
        return;
    }
    json_get_str(body, "username",  username,      sizeof(username));
    json_get_str(body, "password",  user_password, sizeof(user_password));
    json_get_str(body, "camera_id", camera_id,     sizeof(camera_id));

    if (!persist_all(ssid, wifi_password, username, user_password, camera_id)){
        send_json(client, 500, "{\"ok\":false,\"error\":\"persist failed\"}
");
        return;
    }

    send_json(client, 200, "{\"ok\":true,\"msg\":\"saved; switching to STA\"}
");

    pid_t pid=fork();
    if(pid==0){ execl("/bin/sh","sh", WIFI_SH, (char*)NULL); _exit(0); }
}

static void load_cfg(char *ssid, char *username, char *camera_id, int *has_wifi_psk, int *has_user_pass){
    FILE *f=fopen(CFG_TXT_PATH,"r"); ssid[0]=username[0]=camera_id[0]=' '; if(has_wifi_psk) *has_wifi_psk=0; if(has_user_pass) *has_user_pass=0; if(!f) return;
    char line[256]; while(fgets(line,sizeof(line),f)){ char *eq=strchr(line,'='); if(!eq) continue; *eq=' '; char *k=line, *v=eq+1; v[strcspn(v,"
")]=' ';
        if(strcmp(k,"ssid")==0) strncpy(ssid,v,255);
        else if(strcmp(k,"username")==0) strncpy(username,v,255);
        else if(strcmp(k,"camera_id")==0) strncpy(camera_id,v,255);
        else if(strcmp(k,"wifi_psk")==0 && has_wifi_psk) *has_wifi_psk=(v[0]!=' ');
        else if(strcmp(k,"password")==0 && has_user_pass) *has_user_pass=(v[0]!=' '); }
    fclose(f);
}

static void handle_status(int client){
    char ip[64]; const char *mode=detect_mode(ip);
    char ssid[256], username[256], camera_id[256]; int has_wifi_psk=0, has_user_pass=0;
    load_cfg(ssid, username, camera_id, &has_wifi_psk, &has_user_pass);
    char ipq[68], ssidq[260], userq[260], camidq[260];
    json_quote_or_null(ipq,sizeof(ipq), ip[0]?ip:NULL);
    json_quote_or_null(ssidq,sizeof(ssidq), ssid[0]?ssid:NULL);
    json_quote_or_null(userq,sizeof(userq), username[0]?username:NULL);
    json_quote_or_null(camidq,sizeof(camidq), camera_id[0]?camera_id:NULL);
    char json[1024]; snprintf(json,sizeof(json),"{\"ok\":true,\"mode\":\"%s\",\"ip\":%s,\"config\":{\"ssid\":%s,\"username\":%s,\"camera_id\":%s,\"has_wifi_psk\":%s,\"has_user_password\":%s}}
", mode, ipq, ssidq, userq, camidq, has_wifi_psk?"true":"false", has_user_pass?"true":"false");
    send_json(client,200,json);
}

static void* server_thread(void *arg){
    atomic_int *started = (atomic_int*)arg;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0){ perror("socket"); *started = -1; return NULL; }
    int one=1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in serv={0}; serv.sin_family=AF_INET; serv.sin_addr.s_addr=INADDR_ANY; serv.sin_port=htons(TINY_PORT);
    if (bind(sockfd,(struct sockaddr*)&serv,sizeof(serv))<0){ perror("bind"); *started=-1; close(sockfd); return NULL; }
    if (listen(sockfd,8)<0){ perror("listen"); *started=-1; close(sockfd); return NULL; }

    *started = 1; logf_tag("tiny","listening on :%d", TINY_PORT);
    g_run = 1;

    while (g_run){
        struct sockaddr_in cli; socklen_t clilen=sizeof(cli);
        int cfd = accept(sockfd,(struct sockaddr*)&cli,&clilen);
        if (cfd<0){ if(errno==EINTR) continue; break; }

        char req[163840]; int n = recv(cfd, req, sizeof(req)-1, 0);
        if (n<=0){ close(cfd); continue; }
        req[n]=' ';

        int is_post_onboard=0, is_get_status=0;
        if (strncmp(req, "POST /onboard ", 14) == 0 || strstr(req, "POST /onboard ")) is_post_onboard=1;
        else if (strncmp(req, "GET /status ", 12) == 0 || strstr(req, "GET /status ")) is_get_status=1;

        if (!is_post_onboard && !is_get_status){ send_json(cfd,404,"{\"ok\":false,\"error\":\"not found\"}
"); close(cfd); continue; }

        if (is_get_status){ handle_status(cfd); close(cfd); continue; }

        int content_len=0; const char *cl = ci_strstr(req, "Content-Length:");
        if (cl){ cl += strlen("Content-Length:"); while(*cl && (*cl==':'||isspace((unsigned char)*cl))) cl++; content_len = atoi(cl); }
        if (content_len<=0 || content_len>=163840){ send_json(cfd,400,"{\"ok\":false,\"error\":\"invalid Content-Length\"}
"); close(cfd); continue; }

        const char *sep = strstr(req, "

"); char body[163840]; int body_read=0; 
        if (sep){ sep+=4; int already = (int)((req+n)-sep); if(already>0){ if(already>content_len) already=content_len; memcpy(body,sep,already); body_read=already; } }
        while (body_read<content_len){ int r = recv(cfd, body+body_read, content_len-body_read, 0); if(r<=0) break; body_read+=r; if(body_read>=163839) break; }
        body[(body_read<163839)?body_read:163839]=' ';

        handle_onboard(cfd, body);
        close(cfd);
        while (waitpid(-1,NULL,WNOHANG)>0) {}
    }

    close(sockfd);
    *started = 0;
    return NULL;
}

int tiny_server_start(atomic_int *server_started){
    *server_started = 0;
    if (pthread_create(&g_thr,NULL,server_thread,server_started)!=0){ return -1; }
    // caller may poll *server_started to become 1
    return 0;
}

void tiny_server_stop(void){
    if (g_run){ g_run = 0; }
    // poke the accept() with a dummy connection attempt
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s>=0){ struct sockaddr_in a={0}; a.sin_family=AF_INET; a.sin_port=htons(TINY_PORT); a.sin_addr.s_addr=htonl(INADDR_LOOPBACK); connect(s,(struct sockaddr*)&a,sizeof(a)); close(s); }
    pthread_join(g_thr,NULL);
}


// ========================= src/flow.h =========================
#ifndef FLOW_H
#define FLOW_H
void flow_stop_wifi_and_app(void);
int  flow_start_ap_mode(void);
int  flow_start_tiny_server(void);
int  flow_wait_sta_up(void);
int  flow_start_nfs(void);
int  flow_start_app(void);
#endif


// ========================= src/flow.c =========================
#include <string.h>
#include <unistd.h>
#include <stdatomic.h>
#include "config.h"
#include "log.h"
#include "proc.h"
#include "net.h"
#include "tinyserver.h"

static child_t g_app  = {0};
static child_t g_wifi = {0};
static child_t g_ap   = {0};
static child_t g_nfs  = {0};

void flow_stop_wifi_and_app(void) {
    if (g_wifi.pgid) { kill_group(g_wifi.pgid, "wifi.sh"); memset(&g_wifi,0,sizeof(g_wifi)); }
    if (g_app.pgid)  { kill_group(g_app.pgid,  "app");     memset(&g_app,0,sizeof(g_app)); }
    pkill_like("enable_wifi.sh", "pkill");
    pkill_like("keo-cam",        "pkill");
}

int flow_start_ap_mode(void) {
    logf_tag("ap", "starting AP mode...");
    memset(&g_ap,0,sizeof(g_ap));
    if (spawn_sh(AP_MODE_SH, LOG_AP_PATH, &g_ap) != 0) return 0;
    sleep(1);
    return 1;
}

int flow_start_tiny_server(void) {
    static atomic_int started = 0;
    if (tiny_server_start(&started) != 0) return 0;
    // wait up to TIMEOUT_LISTEN_MS for server to set started=1
    int elapsed=0; while (elapsed<TIMEOUT_LISTEN_MS){ if (started==1) { logf_tag("tiny","listening"); return 1; } usleep(100*1000); elapsed+=100; if(started==-1) break; }
    logf_tag("tiny","failed to start"); tiny_server_stop(); return 0;
}

int flow_wait_sta_up(void) {
    logf_tag("net", "waiting for client POST -> wifi.sh -> STA IP...");
    if (!wait_sta_ip(TIMEOUT_STA_MS)) { logf_tag("net", "STA IP timeout"); return 0; }
    tiny_server_stop();
    return 1;
}

int flow_start_nfs(void) {
    logf_tag("nfs", "starting nfs.sh...");
    memset(&g_nfs,0,sizeof(g_nfs));
    if (spawn_sh(NFS_SH, LOG_NFS_PATH, &g_nfs) != 0) return 0;
    if (!wait_mountpoint("/system/nfs", TIMEOUT_NFS_MS)) { logf_tag("nfs", "mountpoint not ready"); return 0; }
    return 1;
}

int flow_start_app(void) {
    logf_tag("app", "starting application...");
    memset(&g_app,0,sizeof(g_app));
    char *argv[] = { (char*)APP_BIN, NULL };
    if (spawn_exec(APP_BIN, argv, LOG_APP_PATH, &g_app) != 0) return 0;
    return 1;
}


// ========================= src/orchestrator_main.c =========================
#include <time.h>
#include <unistd.h>
#include "config.h"
#include "log.h"
#include "gpio_btn.h"
#include "flow.h"

int main(void) {
    gpio_init();

    time_t last_trigger = 0;

    for (;;) {
        if (!gpio_wait_long_press()) continue; // blocks until edge, filters short press

        time_t now = time(NULL);
        if (now - last_trigger < RETRIGGER_GUARD_S) {
            logf_tag("boot-btn", "ignored (guard %ds)", (int)RETRIGGER_GUARD_S);
            continue;
        }
        last_trigger = now;

        logf_tag("flow", "Starting onboarding sequence");

        flow_stop_wifi_and_app();           // 1) stop wifi + app
        if (!flow_start_ap_mode())   continue; // 2) AP
        if (!flow_start_tiny_server()) continue; // 3) tiny server (embedded)
        if (!flow_wait_sta_up())     continue; // 4) wait STA IP (after POST)
        if (!flow_start_nfs())       continue; // 5) NFS
        if (!flow_start_app())       continue; // 6) App

        logf_tag("flow", "Onboarding flow completed.");
    }
    return 0;
}
