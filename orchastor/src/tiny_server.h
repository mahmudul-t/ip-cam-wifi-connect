#ifndef TINY_SERVER_H
#define TINY_SERVER_H
#include <stdatomic.h>
int tiny_server_start(atomic_int *server_started);
void tiny_server_stop(void);
#endif
