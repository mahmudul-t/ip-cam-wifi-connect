
// ========================= src/flow.c =========================
#include <string.h>
#include <unistd.h>
#include <stdatomic.h>
#include "config.h"
#include "log.h"
#include "proc.h"
#include "net.h"
#include "tiny_server.h"

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


int flow_wait_sta_up(void) {
    logf_tag("net", "waiting for client POST -> wifi.sh -> STA IP...");
    if (!wait_sta_ip(TIMEOUT_STA_MS)) { logf_tag("net", "STA IP timeout"); return 0; }
    tiny_server_stop();
    // Start background internet check thread or loop here
    return 1;
}