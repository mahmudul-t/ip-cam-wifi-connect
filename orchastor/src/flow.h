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