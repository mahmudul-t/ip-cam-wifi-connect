#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <pthread.h>


#define GPIO_NUM "64"
#define GPIO_PATH "/sys/class/gpio/gpio64/"
#define AP_MODE_SCRIPT "/system/www/ap_mode_enable.sh"
#define ENABLE_WIFI_SCRIPT "/system/www/enable_wifi.sh"

#define APP_NAME "keo-cam"  

static int ap_mode_active = 0; // 0 = normal STA mode, 1 = AP mode


void export_gpio() {
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd >= 0) {
        write(fd, GPIO_NUM, strlen(GPIO_NUM));
        close(fd);
    }
    usleep(200000);  // wait 200ms for sysfs to populate
}

void unexport_gpio() {
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd >= 0) {
        write(fd, GPIO_NUM, strlen(GPIO_NUM));
        close(fd);
    }
}

void set_gpio_direction() {
    int fd = open(GPIO_PATH "direction", O_WRONLY);
    if (fd >= 0) {
        write(fd, "in", 2);
        close(fd);
    }
}

void set_gpio_edge() {
    int fd = open(GPIO_PATH "edge", O_WRONLY);
    if (fd >= 0) {
        write(fd, "falling", 7);  // falling edge = button pressed (active low)
        close(fd);
    }
}

int read_gpio_value() {
    int fd = open(GPIO_PATH "value", O_RDONLY);
    if (fd < 0) return -1;

    char buf;
    read(fd, &buf, 1);
    close(fd);
    return (buf == '0') ? 0 : 1;
}

////////////////////////////////////

static pid_t ap_pid = -1;   // store child pid
static pid_t wifi_pid = -1;   // store child pid

static int run_ap_script(void){
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", AP_MODE_SCRIPT, (char*)NULL);
        _exit(127); // exec failed
    }
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    ap_pid = pid;  // save child pid
    return 0; // parent returns immediately
}

static int run_wifi_script(void)
{
    pid_t pid = fork();
    if (pid == 0) 
    {
        execl("/bin/sh", "sh", ENABLE_WIFI_SCRIPT, (char*)NULL);
        _exit(127); // exec failed
    }
    if (pid < 0) 
    {
        perror("fork");
        return -1;
    }

    wifi_pid = pid;
    return 0; // parent returns immediately
}


static int stop_wifi_script(void) 
{
    if (wifi_pid > 0) {
        if (kill(wifi_pid, SIGTERM) == 0) {
            printf("Sent SIGTERM to %d\n", wifi_pid);
            wifi_pid = -1;
            return 0;
        } else {
            perror("kill");
            return -1;
        }
    }
    return -1; // no process running
}

 

int stop_application(void)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "pgrep %s", APP_NAME);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        perror("popen");
        return -1;
    }

    char pid_str[16];
    if (fgets(pid_str, sizeof(pid_str), fp) != NULL) 
    {
        pid_t pid = (pid_t)atoi(pid_str);
        printf("Found %s PID: %d\n", APP_NAME, pid);

        if (kill(pid, SIGTERM) == 0) 
        {
            printf("Sent SIGTERM to %s\n", APP_NAME);
        } 
        else 
        {
            perror("kill");
        }
    } 
    else 
    {
        printf("%s not running\n", APP_NAME);
    }

    pclose(fp);
    return 0;
}



// int main() {

//     export_gpio();
//     set_gpio_direction();
//     set_gpio_edge();

//     int fd = open(GPIO_PATH "value", O_RDONLY);
//     if (fd < 0) {
//         perror("Failed to open GPIO value");
//         return 1;
//     }

//     struct pollfd pfd;
//     pfd.fd = fd;
//     pfd.events = POLLPRI | POLLERR;

//     char buf[8];

//     printf("[BOOT BTN] Monitoring GPIO64 for long press...\n");
    
//     run_wifi_script();


//     while (1) {
//         // Clear old value
//         lseek(fd, 0, SEEK_SET);
//         read(fd, buf, sizeof(buf));

//         int ret = poll(&pfd, 1, -1);  // wait forever for falling edge
//         if (ret > 0 && (pfd.revents & POLLPRI)) {
//             time_t press_start = time(NULL);

//             // Wait 2 seconds and check if still pressed
//             sleep(2);

//             if (read_gpio_value() == 0) {
//                 printf("[BOOT BTN] Long press detected. Stop wifi.... Launching AP mode ...\n");
//                 stop_wifi_script();
//                 stop_application();
//                 run_ap_script();
//                 sleep(20);  // prevent rapid retriggering
//             } else {
//                 printf("[BOOT BTN] Short press ignored.\n");
//             }
//         }
//     }

//     close(fd);
//     unexport_gpio();
//     return 0;
// }


void *button_thread(void *arg)
{
    // all your GPIO export/direction/edge setup here
    export_gpio();
    set_gpio_direction();
    set_gpio_edge();

    int fd = open(GPIO_PATH "value", O_RDONLY);
    if (fd < 0) {
        perror("Failed to open GPIO value");
        return NULL;
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLPRI | POLLERR;

    char buf[8];

    printf("[BOOT BTN] Monitoring GPIO64 for long press...\n");

    while (1) {
        // Clear old value
        lseek(fd, 0, SEEK_SET);
        read(fd, buf, sizeof(buf));

        int ret = poll(&pfd, 1, -1);  // now can block forever

        if (ret > 0 && (pfd.revents & POLLPRI)) {
            time_t press_start = time(NULL);

            // Wait 2 seconds and check if still pressed
            sleep(2);

            if (read_gpio_value() == 0) {
                printf("[BOOT BTN] Long press detected. Stop wifi.... Launching AP mode ...\n");
                stop_wifi_script();
                stop_application();
                run_ap_script();
                ap_mode_active = 1;  // mark that we’re in AP mode now
            } else {
                printf("[BOOT BTN] Short press ignored.\n");
            }
        }
    }

    close(fd);
    unexport_gpio();
    return NULL;
}


void wifi_soft_reconnect(void)
{
    system("/system/tools/wifi/wpa_cli -i wlan0 reconnect");
}


// Returns 1 if connected, 0 if not.
static int is_wifi_connected(void)
{
    int ret = system("ping -c 1 -W 1 8.8.8.8 >/dev/null 2>&1");
    if (ret == 0) {
        // ping success
        return 1;
    }
    return 0;
}


void *wifi_watchdog_thread(void *arg)
{
    while (1) {
        sleep(60);  // wait 1 minute

        if (ap_mode_active) {
            // In AP mode, you usually don't want to force STA reconnect
            continue;
        }

        if (!is_wifi_connected()) {  // implement however you prefer (ping / link / hybrid)
            printf("[WiFi] Disconnected. Trying to reconnect...\n");

            // Only start new script if nothing already running
            // if (wifi_pid <= 0) 
            // {
                wifi_soft_reconnect();

                // if (is_wifi_connected())
                // {
                //     printf("[WiFi] Reconnected..\n");
                // }
                // else
                // {
                //     printf("[WiFi] Reconnection Failed..\n");
                // }
                
            // } 
            // else 
            // {
            //     printf("[WiFi] Script already running with pid %d, skip restart.\n", wifi_pid);
            // }
        } 
        else 
        {
            printf("[WiFi] Still connected.\n");
        }
    }

    return NULL;
}


int main()
{
    pthread_t tid_btn, tid_wifi;
    run_wifi_script();

    if(pthread_create(&tid_btn,NULL,button_thread, NULL) != 0)
    {
        perror("pthread create button thread\n");
    }

    if(pthread_create(&tid_wifi, NULL, wifi_watchdog_thread, NULL)!= 0)
    {
        perror("error pthead wifi\n");
    }

    pthread_join(tid_btn, NULL);
    pthread_join(tid_wifi, NULL);

    return 0;
}