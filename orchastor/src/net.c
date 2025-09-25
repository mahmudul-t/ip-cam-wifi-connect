
// ========================= src/net.c =========================
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <stdio.h>
#include <errno.h>
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
    out[0] = '\0';
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

int check_internet(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 0;
    struct sockaddr_in addr; memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET; addr.sin_port=htons(INTERNET_CHECK_PORT);
    if (inet_pton(AF_INET, INTERNET_CHECK_HOST, &addr.sin_addr) <= 0) { close(s); return 0; }
    int r = connect(s,(struct sockaddr*)&addr,sizeof(addr));
    close(s);
    return (r==0);
}