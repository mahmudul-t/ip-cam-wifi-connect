// ========================= src/orchestrator_main.c (internet loop added) =========================
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include "config.h"
#include "log.h"
#include "gpio_btn.h"
#include "flow.h"
#include "net.h"

static void* internet_thread(void *arg){
    (void)arg;
    for(;;){
        sleep(INTERNET_CHECK_INTERVAL);
        if (check_internet()) logf_tag("inet","Internet is available");
        else logf_tag("inet","No internet");
    }
    return NULL;
}

int main(void) {
    gpio_init();
    pthread_t inet_thr; int inet_started=0;

    time_t last_trigger = 0;

    for (;;) {
        if (!gpio_wait_long_press()) continue;
        // time_t now = time(NULL);
        // if (now - last_trigger < RETRIGGER_GUARD_S) { continue; }
        // last_trigger = now;

        logf_tag("flow", "Starting onboarding sequence");
        printf("flow stop and onboarding sequence\n");
        flow_stop_wifi_and_app();
        if (!flow_start_ap_mode())   continue;
        if (!flow_start_tiny_server()) continue;
        if (!flow_wait_sta_up())     continue;
        if (!flow_start_nfs())       continue;
        if (!flow_start_app())       continue;

        if (!inet_started){ pthread_create(&inet_thr,NULL,internet_thread,NULL); inet_started=1; }

        logf_tag("flow", "Onboarding flow completed.");
    }
    return 0;
}
