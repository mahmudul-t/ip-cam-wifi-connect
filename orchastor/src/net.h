// ========================= src/net.h =========================
#ifndef NET_H
#define NET_H
int wait_port_listen(int port, int timeout_ms);
int wait_sta_ip(int timeout_ms);
int wait_mountpoint(const char *mp, int timeout_ms);
int check_internet(void);
#endif
