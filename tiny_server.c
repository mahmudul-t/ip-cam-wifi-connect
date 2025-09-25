/* tiny_onboard_server.c
 * Endpoints:
 *   POST /onboard   JSON: ssid, wifi_password, username, password, camera_id
 *   GET  /status    JSON: mode(AP/STA/UNKNOWN), ip, saved meta (no passwords)
 *
 * Files:
 *   - /system/etc/device_wifi_config.txt  (plain key=value)
 *   - (optional) /etc/wpa_supplicant.conf (uses ssid + wifi_password)
 *
 * Build (cross):  mips-linux-gnu-gcc -O2 -Wall -static -DDEBUG=1 -o tiny_server tiny_onboard_server.c
 * Build (native): gcc -O2 -Wall -DDEBUG=1 -o tiny_server tiny_onboard_server.c
 */

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

#ifndef DEBUG
#define DEBUG 1
#endif

#if DEBUG
  #define LOG(fmt, ...) fprintf(stderr, "[tiny] " fmt "\n", ##__VA_ARGS__)
#else
  #define LOG(fmt, ...) do{}while(0)
#endif

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
static void mask_secret(const char *in, char *out, size_t outsz){
    size_t n = in ? strlen(in) : 0;
    if (!in || !n){ snprintf(out,outsz,"<empty>"); return; }
    if (n <= 4){ snprintf(out,outsz,"****"); return; }
    snprintf(out,outsz,"%.*s****%.*s", 2, in, 2, in + (int)n - 2);
}

/* case-insensitive strstr */
static const char* ci_strstr(const char *haystack, const char *needle){
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

static int safe_write(const char *path, const char *content, mode_t mode){
    char tmp[STR_MAX];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if(!f){
        LOG("safe_write: fopen(%s) failed: %s", tmp, strerror(errno));
        return 0;
    }
    if (fputs(content, f) == EOF){
        LOG("safe_write: fputs to %s failed: %s", tmp, strerror(errno));
        fclose(f);
        unlink(tmp);
        return 0;
    }
    if (fclose(f) != 0){
        LOG("safe_write: fclose(%s) failed: %s", tmp, strerror(errno));
        unlink(tmp);
        return 0;
    }
    if (chmod(tmp, mode) != 0){
        LOG("safe_write: chmod(%s, 0%o) failed: %s", tmp, (unsigned)mode, strerror(errno));
        unlink(tmp);
        return 0;
    }
    if (rename(tmp, path) != 0){
        LOG("safe_write: rename(%s -> %s) failed: %s", tmp, path, strerror(errno));
        unlink(tmp);
        return 0;
    }
    LOG("safe_write: wrote %s (%zu bytes)", path, strlen(content));
    return 1;
}

/* tiny JSON string extractor: finds "key":"value" */
static int json_get_str(const char *json, const char *key, char *out, size_t outsz){
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
    while (*p && *p != '\"' && i < outsz-1) {
        if (*p == '\\' && *(p+1)) p++; // skip escape simply
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

/* JSON quote helper: writes "str" or null */
static void json_quote_or_null(char *out, size_t outsz, const char *str){
    if (!str || !str[0]) { snprintf(out, outsz, "null"); return; }
    char buf[STR_MAX]; size_t j = 0;
    for (size_t i=0; str[i] && j < sizeof(buf)-2; i++){
        char c = str[i];
        if (c == '\"' || c == '\\') { if (j < sizeof(buf)-2) buf[j++]='\\'; }
        buf[j++] = c;
    }
    buf[j] = '\0';
    snprintf(out, outsz, "\"%s\"", buf);
}

/* Read IPv4 via ioctl; returns 1 if found */
static int get_iface_ip(char ipbuf[STR_MAX]){
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0){ LOG("socket(AF_INET,SOCK_DGRAM) failed: %s", strerror(errno)); return 0; }
    struct ifreq ifr; memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, IFACE, IFNAMSIZ-1);
    if (ioctl(fd, SIOCGIFADDR, &ifr) == 0){
        struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
        const char *ip = inet_ntoa(sin->sin_addr);
        if (ip && strcmp(ip, "0.0.0.0") != 0){
            strncpy(ipbuf, ip, STR_MAX-1);
            close(fd);
            return 1;
        }
    } else {
        LOG("ioctl(SIOCGIFADDR) on %s failed: %s", IFACE, strerror(errno));
    }
    close(fd);
    return 0;
}

/* Determine mode */
static const char* detect_mode(char ip_out[STR_MAX]){
    ip_out[0] = '\0';
    if (access("/var/run/hostapd/hostapd.pid", F_OK) == 0) {
        (void)get_iface_ip(ip_out);
        LOG("detect_mode: hostapd pid present, ip=%s", ip_out[0]?ip_out:"(none)");
        return "AP";
    }
    if (get_iface_ip(ip_out)) {
        const char *m = (strcmp(ip_out, "192.168.4.1") == 0) ? "AP" : "STA";
        LOG("detect_mode: iface ip=%s -> %s", ip_out, m);
        return m;
    }
    LOG("detect_mode: UNKNOWN (no IP)");
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

    LOG("persist_all: writing config to %s", CFG_TXT_PATH);
    if (!safe_write(CFG_TXT_PATH, txt, 0600)) {
        LOG("persist_all: failed writing %s", CFG_TXT_PATH);
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
    LOG("persist_all: writing WPA file %s", WPA_PATH);
    if (!safe_write(WPA_PATH, wpa, 0600)) {
        LOG("persist_all: failed writing %s", WPA_PATH);
        return 0;
    }
#else
    LOG("persist_all: WPA write disabled (ENABLE_WPA_WRITE=0)");
#endif

    return 1;
}

/* send JSON response with code */
static void send_json(int client, int code, const char *json){
    char header[256];
    snprintf(header, sizeof(header),
        "HTTP/1.1 %d\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n",
        code, json ? strlen(json) : 0);
    send(client, header, strlen(header), 0);
    if (json) send(client, json, strlen(json), 0);
}

/* ---------- POST /onboard ---------- */
static void handle_onboard(int client, const char *body){
    char ssid[STR_MAX]="", wifi_password[STR_MAX]="", username[STR_MAX]="", user_password[STR_MAX]="", camera_id[STR_MAX]="";
    LOG("handle_onboard: body len ~%zu", strlen(body));

    if (!json_get_str(body, "ssid", ssid, sizeof(ssid)) ||
        !json_get_str(body, "wifi_password", wifi_password, sizeof(wifi_password))) {
        LOG("handle_onboard: missing ssid or wifi_password");
        send_json(client, 400, "{\"ok\":false,\"error\":\"missing ssid or wifi_password\"}\n");
        return;
    }
    json_get_str(body, "username",  username,      sizeof(username));
    json_get_str(body, "password",  user_password, sizeof(user_password));
    json_get_str(body, "camera_id", camera_id,     sizeof(camera_id));

    char wifi_pw_mask[64], user_pw_mask[64];
    mask_secret(wifi_password, wifi_pw_mask, sizeof(wifi_pw_mask));
    mask_secret(user_password, user_pw_mask, sizeof(user_pw_mask));
    LOG("handle_onboard: ssid='%s', wifi_password='%s', username='%s', password='%s', camera_id='%s'",
        ssid, wifi_pw_mask, username[0]?username:"(none)", user_pw_mask, camera_id[0]?camera_id:"(none)");

    if (!persist_all(ssid, wifi_password, username, user_password, camera_id)){
        LOG("handle_onboard: persist_all FAILED");
        send_json(client, 500, "{\"ok\":false,\"error\":\"persist failed\"}\n");
        return;
    }

    

    send_json(client, 200, "{\"ok\":true,\"msg\":\"saved; switching to STA\"}\n");


   pid_t pid = fork();
    if (pid == 0) {
        LOG("handle_onboard: exec /system/www/enable_wifi.sh");
        execl("/bin/sh", "sh", "/system/www/enable_wifi.sh", (char*)NULL);
        LOG("handle_onboard: exec failed: %s", strerror(errno));
        _exit(0);
    } else if (pid > 0) {
        LOG("handle_onboard: spawned enable_wifi.sh pid=%d", (int)pid);
    } else {
        LOG("handle_onboard: fork failed: %s", strerror(errno));
    }

}

/* parse key=value lines from CFG_TXT_PATH */
static void load_cfg(char *ssid, char *username, char *camera_id, int *has_wifi_psk, int *has_user_pass){
    FILE *f = fopen(CFG_TXT_PATH, "r");
    ssid[0]=username[0]=camera_id[0]='\0';
    if (has_wifi_psk) *has_wifi_psk = 0;
    if (has_user_pass) *has_user_pass = 0;
    if (!f){
        LOG("load_cfg: fopen(%s) failed: %s", CFG_TXT_PATH, strerror(errno));
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)){
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
    LOG("load_cfg: ssid='%s', username='%s', camera_id='%s', has_wifi_psk=%d, has_user_pass=%d",
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

    LOG("handle_status: mode=%s ip=%s", mode, ip[0]?ip:"(none)");
    send_json(client, 200, json);
}

int main(void){
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0){ perror("socket"); return 1; }
    int one = 1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in serv = {0};
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr*)&serv, sizeof(serv)) < 0){
        perror("bind");
        LOG("bind failed on port %d. Is something else listening?", PORT);
        return 1;
    }
    if (listen(sockfd, 8) < 0){ perror("listen"); return 1; }

    LOG("tiny_onboard_server listening on 0.0.0.0:%d", PORT);
    printf("tiny_onboard_server listening on 0.0.0.0:%d\n", PORT);

    for (;;) {
        struct sockaddr_in cli; socklen_t clilen = sizeof(cli);
        int cfd = accept(sockfd, (struct sockaddr*)&cli, &clilen);
        if (cfd < 0) { LOG("accept failed: %s", strerror(errno)); continue; }

        LOG("client: %s:%d connected", inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));

        char req[RECV_BUFSZ]; int n = recv(cfd, req, sizeof(req)-1, 0);
        if (n <= 0){ LOG("recv <=0 (%d), closing", n); close(cfd); continue; }
        req[n] = '\0';

        /* log first line */
        char *eol = strstr(req, "\r\n");
        if (eol){ *eol = '\0'; LOG("request-line: %s", req); *eol = '\r'; }
        else     { LOG("request-chunk: %.80s", req); }

        int is_post_onboard = 0, is_get_status = 0;
        if (strncmp(req, "POST /onboard ", 14) == 0 || strstr(req, "POST /onboard "))
            is_post_onboard = 1;
        else if (strncmp(req, "GET /status ", 12) == 0 || strstr(req, "GET /status "))
            is_get_status = 1;

        if (!is_post_onboard && !is_get_status) {
            LOG("no matching endpoint");
            send_json(cfd, 404, "{\"ok\":false,\"error\":\"not found\"}\n");
            close(cfd);
            continue;
        }

        if (is_get_status){
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
        LOG("Content-Length: %d", content_len);
        if (content_len <= 0 || content_len >= BODY_BUFSZ){
            LOG("invalid Content-Length");
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
        while (body_read < content_len) {
            int r = recv(cfd, body + body_read, content_len - body_read, 0);
            if (r <= 0) break;
            body_read += r;
            if (body_read >= (BODY_BUFSZ - 1)) break;
        }
        body[(body_read < BODY_BUFSZ-1) ? body_read : (BODY_BUFSZ-1)] = '\0';
        LOG("body_read: %d bytes", body_read);

        handle_onboard(cfd, body);
        close(cfd);

        /* reap any children (switch script) */
        while (waitpid(-1, NULL, WNOHANG) > 0) {}
    }

    close(sockfd);
    return 0;
}
