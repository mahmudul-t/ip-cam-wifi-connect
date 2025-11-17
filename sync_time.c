/*  sync_time.c - Robust NTP sync with timeout, retry, and safe Ctrl+C */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>

typedef struct {
    uint8_t  li_vn_mode;
    uint8_t  stratum;
    uint8_t  poll;
    int8_t   precision;
    uint32_t root_delay;
    uint32_t root_dispersion;
    uint32_t ref_id;
    uint32_t ref_ts_sec;
    uint32_t ref_ts_frac;
    uint32_t orig_ts_sec;
    uint32_t orig_ts_frac;
    uint32_t rx_ts_sec;
    uint32_t rx_ts_frac;
    uint32_t tx_ts_sec;
    uint32_t tx_ts_frac;
} __attribute__((packed)) ntp_packet_t;

/* Config */
static const char *NTP_SERVER = "pool.ntp.org";
static const int   PRINT_INTERVAL = 5;
static const int   SYNC_INTERVAL  = 10;
static const int   TIMEOUT_SEC    = 5;
static const int   MAX_RETRIES    = 3;

static volatile sig_atomic_t keep_running = 1;

/* Ctrl+C handler */
static void sig_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

/* DNS resolve */
static int resolve_host(const char *host, struct in_addr *addr) {
    struct addrinfo hints = {0}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    int ret = getaddrinfo(host, "123", &hints, &res);
    if (ret != 0) {
        fprintf(stderr, "getaddrinfo(%s): %s\n", host, gai_strerror(ret));
        return -1;
    }

    for (struct addrinfo *p = res; p; p = p->ai_next) {
        if (p->ai_family == AF_INET) {
            *addr = ((struct sockaddr_in*)p->ai_addr)->sin_addr;
            freeaddrinfo(res);
            return 0;
        }
    }
    freeaddrinfo(res);
    return -1;
}

/* Set socket timeout */
static int set_socket_timeout(int sock, int sec) 
{
    struct timeval tv = { .tv_sec = sec, .tv_usec = 0 };
    return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

/* NTP sync with retry and timeout */
static int ntp_sync(void) 
{
    struct sockaddr_in srv = {0};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(123);

    if (resolve_host(NTP_SERVER, &srv.sin_addr) != 0) 
    {
        fprintf(stderr, "[NTP] DNS resolve failed\n");
        return -1;
    }

    printf("[NTP] Resolved %s -> %s\n", NTP_SERVER, inet_ntoa(srv.sin_addr));

    ntp_packet_t pkt = {0};
    pkt.li_vn_mode = 0x1B;

    for (int retry = 0; retry < MAX_RETRIES; retry++) 
    {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) { perror("socket"); return -1; }

        // Set receive timeout
        if (set_socket_timeout(sock, TIMEOUT_SEC) < 0) {
            perror("setsockopt timeout");
            close(sock);
            continue;
        }

        printf("[NTP] Sending request (attempt %d/%d)...\n", retry+1, MAX_RETRIES);
        if (sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr*)&srv, sizeof(srv)) < 0) 
        {
            perror("sendto");
            close(sock);
            continue;
        }

        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t rc = recvfrom(sock, &pkt, sizeof(pkt), 0, (struct sockaddr*)&from, &flen);

        if (rc < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) 
            {
                printf("[NTP] Timeout waiting for response\n");
            } else {
                perror("recvfrom");
            }
            close(sock);
            continue;  // retry
        }

        close(sock);

        // Validate response
        if (pkt.stratum == 0) 
        {
            printf("[NTP] Server refused\n");
            continue;
        }

        uint32_t ntp_sec  = ntohl(pkt.tx_ts_sec);
        uint32_t ntp_frac = ntohl(pkt.tx_ts_frac);
        uint64_t unix_sec = (uint64_t)ntp_sec - 2208988800ULL;
        uint64_t total_us = unix_sec * 1000000ULL + ((uint64_t)ntp_frac * 1000000ULL / (1ULL<<32));

        struct timeval tv = 
        {
            .tv_sec  = total_us / 1000000ULL,
            .tv_usec = total_us % 1000000ULL
        };

        if (settimeofday(&tv, NULL) < 0) 
        {
            perror("settimeofday");
            return -1;
        }

        printf("[NTP] SYNC OK -> %s", ctime((time_t*)&unix_sec));
        return 0;
    }

    fprintf(stderr, "[NTP] All retries failed\n");
    return -1;
}

/* Print time */
static void print_current_time(void) 
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0) return;

    struct tm *tm = gmtime(&tv.tv_sec);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    printf("[TIME] %s.%06ld UTC\n", buf, tv.tv_usec);
}

/* Main loop */
int main(void) {
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    printf("=== Robust NTP Sync & Print ===\n");
    printf("Server: %s | Print: %ds | Sync: %ds\n\n", NTP_SERVER, PRINT_INTERVAL, SYNC_INTERVAL);

    // Initial sync
    if (ntp_sync() != 0) 
    {
        fprintf(stderr, "Initial sync failed. Will retry every %d s.\n", SYNC_INTERVAL);
    }

    time_t last_print = time(NULL);
    time_t last_sync  = last_print;

    while (keep_running) 
    {
        time_t now = time(NULL);

        if (now - last_print >= PRINT_INTERVAL) 
        {
            print_current_time();
            last_print = now;
        }

        if (now - last_sync >= SYNC_INTERVAL) 
        {
            ntp_sync();  // retry even if failed
            last_sync = now;
        }

        sleep(2);  // 0.2s
    }

    printf("\nShutdown.\n");
    return 0;
}