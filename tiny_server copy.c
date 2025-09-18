/* tiny_onboard_server.c
 * Minimal HTTP server with two endpoints:
 *   POST /onboard   (JSON: ssid, psk, username, device_id, token?)
 *   GET  /status    (mode: AP/STA/UNKNOWN, ip, saved config meta)
 *
 * Files:
 *   - /system/etc/device_wifi_config.txt  (plain text key=value)
 *   - /etc/wpa_supplicant.conf            (for STA connect)
 *   - runs /usr/local/bin/switch_to_sta.sh after onboarding
 *
 * Build (cross): mips-linux-gnu-gcc -O2 -Wall -static -o tiny_server tiny_onboard_server.c
 * Build (native): gcc -O2 -Wall -o tiny_server tiny_onboard_server.c
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

#define PORT        80
#define RECV_BUFSZ  16384
#define BODY_BUFSZ  16384
#define STR_MAX     256

#define CFG_TXT_PATH "/system/etc/device_wifi_config.txt"
#define WPA_PATH     "/etc/wpa_supplicant.conf"
#define IFACE        "wlan0"

/* ---------------- Small helpers ---------------- */

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
    if(!f) return 0;
    fputs(content, f);
    fclose(f);
    chmod(tmp, mode);
    return (rename(tmp, path) == 0);
}

/* Very small JSON string extractor: looks for "key":"value" */
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
    while (*p && *p != '\"' && i < outsz-1) {
        if (*p == '\\' && *(p+1)) p++; // skip escape simply
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

/* JSON quote helper: writes "str" or null into out */
static void json_quote_or_null(char *out, size_t outsz, const char *str)
{
    if (!str || !str[0]) { snprintf(out, outsz, "null"); return; }
    /* Very light escaping of quotes and backslashes */
    char buf[STR_MAX];
    size_t j = 0;
    for (size_t i=0; str[i] && j < sizeof(buf)-2; i++){
        char c = str[i];
        if (c == '\"' || c == '\\') { if (j < sizeof(buf)-2) buf[j++]='\\'; }
        buf[j++] = c;
    }
    buf[j] = '\0';
    snprintf(out, outsz, "\"%s\"", buf);
}

/* Read IPv4 of interface via ioctl; returns 1 if found */
static int get_iface_ip(char ipbuf[STR_MAX]){
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
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
    }
    close(fd);
    return 0;
}

/* Determine mode:
 *  - If hostapd pid exists -> AP
 *  - else if iface IP == 192.168.4.1 -> AP
 *  - else if iface has any other IP -> STA
 *  - else UNKNOWN
 */
static const char* detect_mode(char ip_out[STR_MAX]){
    ip_out[0] = '\0';
    if (access("/var/run/hostapd/hostapd.pid", F_OK) == 0) {
        get_iface_ip(ip_out);
        return "AP";
    }
    if (get_iface_ip(ip_out)) {
        if (strcmp(ip_out, "192.168.4.1") == 0) return "AP";
        return "STA";
    }
    return "UNKNOWN";
}

/* Persist configs to file + wpa_supplicant */
static int persist_all(const char *ssid, const char *psk,
                       const char *username, const char *device_id,
                       const char *token){
    /* /system/etc/device_wifi_config.txt (plain) */
    char txt[1024];
    snprintf(txt, sizeof(txt),
        "ssid=%s\n"
        "psk=%s\n"
        "username=%s\n"
        "device_id=%s\n"
        "token=%s\n",
        ssid, psk, username, device_id, token ? token : "");
    if (!safe_write(CFG_TXT_PATH, txt, 0600)) return 0;

    /* /etc/wpa_supplicant.conf */
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
        ssid, psk);
    if (!safe_write(WPA_PATH, wpa, 0600)) return 0;

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

/* ---------- Endpoint: POST /onboard ---------- */
static void handle_onboard(int client, const char *body){
    char ssid[STR_MAX]="", psk[STR_MAX]="", username[STR_MAX]="", device_id[STR_MAX]="", token[STR_MAX]="";

    if (!json_get_str(body, "ssid", ssid, sizeof(ssid)) ||
        !json_get_str(body, "psk", psk, sizeof(psk))) {
        send_json(client, 400, "{\"ok\":false,\"error\":\"missing ssid or psk\"}\n");
        return;
    }
    json_get_str(body, "username",  username,  sizeof(username));
    json_get_str(body, "device_id", device_id, sizeof(device_id));
    json_get_str(body, "token",     token,     sizeof(token));

    if (!persist_all(ssid, psk, username, device_id, token)){
        send_json(client, 500, "{\"ok\":false,\"error\":\"persist failed\"}\n");
        return;
    }

    /* fire-and-forget switch script */
    if (fork() == 0){
        execl("/bin/sh", "sh", "/usr/local/bin/switch_to_sta.sh", (char*)NULL);
        _exit(0);
    }

    send_json(client, 200, "{\"ok\":true,\"msg\":\"saved; switching to STA\"}\n");
}

/* Parse key=value lines from CFG_TXT_PATH (simple) */
static void load_cfg(char *ssid, char *username, char *device_id, int *has_psk){
    FILE *f = fopen(CFG_TXT_PATH, "r");
    ssid[0]=username[0]=device_id[0]='\0'; if (has_psk) *has_psk=0;
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)){
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = '\0';
        char *k = line, *v = eq+1;
        /* trim newline */
        v[strcspn(v, "\r\n")] = '\0';
        if (strcmp(k, "ssid")==0)           strncpy(ssid, v, STR_MAX-1);
        else if (strcmp(k, "username")==0)  strncpy(username, v, STR_MAX-1);
        else if (strcmp(k, "device_id")==0) strncpy(device_id, v, STR_MAX-1);
        else if (strcmp(k, "psk")==0 && has_psk) *has_psk = (v[0] != '\0');
    }
    fclose(f);
}

/* ---------- Endpoint: GET /status ---------- */
static void handle_status(int client){
    char ip[STR_MAX]; const char *mode = detect_mode(ip);

    char ssid[STR_MAX], username[STR_MAX], device_id[STR_MAX]; int has_psk = 0;
    load_cfg(ssid, username, device_id, &has_psk);

    char ipq[STR_MAX+2];      json_quote_or_null(ipq, sizeof(ipq), ip[0]?ip:NULL);
    char ssidq[STR_MAX+2];    json_quote_or_null(ssidq, sizeof(ssidq), ssid[0]?ssid:NULL);
    char userq[STR_MAX+2];    json_quote_or_null(userq, sizeof(userq), username[0]?username:NULL);
    char devidq[STR_MAX+2];   json_quote_or_null(devidq, sizeof(devidq), device_id[0]?device_id:NULL);

    char json[1024];
    snprintf(json, sizeof(json),
        "{"
          "\"ok\":true,"
          "\"mode\":\"%s\","
          "\"ip\":%s,"
          "\"config\":{"
            "\"ssid\":%s,"
            "\"username\":%s,"
            "\"device_id\":%s,"
            "\"has_psk\":%s"
          "}"
        "}\n",
        mode, ipq, ssidq, userq, devidq, has_psk ? "true":"false");

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

    if (bind(sockfd, (struct sockaddr*)&serv, sizeof(serv)) < 0){ perror("bind"); return 1; }
    if (listen(sockfd, 8) < 0){ perror("listen"); return 1; }

    printf("tiny_onboard_server listening on 0.0.0.0:%d\n", PORT);

    for (;;) {
        struct sockaddr_in cli; socklen_t clilen = sizeof(cli);
        int cfd = accept(sockfd, (struct sockaddr*)&cli, &clilen);
        if (cfd < 0) continue;

        char req[RECV_BUFSZ]; int n = recv(cfd, req, sizeof(req)-1, 0);
        if (n <= 0){ close(cfd); continue; }
        req[n] = '\0';

        /* find request line and method/path */
        int is_post_onboard = 0, is_get_status = 0;
        if (strncmp(req, "POST /onboard ", 14) == 0 || strstr(req, "POST /onboard "))
            is_post_onboard = 1;
        else if (strncmp(req, "GET /status ", 12) == 0 || strstr(req, "GET /status "))
            is_get_status = 1;

        if (!is_post_onboard && !is_get_status) {
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

        /* POST /onboard: need Content-Length and body */
        int content_len = 0;
        const char *cl = ci_strstr(req, "Content-Length:");
        if (cl){
            cl += strlen("Content-Length:");
            while (*cl && (*cl==':' || isspace((unsigned char)*cl))) cl++;
            content_len = atoi(cl);
        }
        if (content_len <= 0 || content_len >= BODY_BUFSZ){
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

        handle_onboard(cfd, body);
        close(cfd);

        /* reap any children (switch script) */
        while (waitpid(-1, NULL, WNOHANG) > 0) {}
    }

    close(sockfd);
    return 0;
}
