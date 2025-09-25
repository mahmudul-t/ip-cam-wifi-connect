// ========================= src/config.h =========================
#ifndef CONFIG_H
#define CONFIG_H
#define IFACE               "wlan0"
#define GPIO_NUM_STR        "64"
#define GPIO_DIRPATH        "/sys/class/gpio/gpio64/"
#define WIFI_SH             "/system/www/enable_wifi.sh"
#define AP_MODE_SH          "/system/www/ap_mode_enable.sh"
#define NFS_SH              "/system/start_nfs.sh"
#define APP_BIN             "/system/nfs/keo-cam"
#define TINY_PORT           8080
#define CFG_TXT_PATH        "/system/etc/device_wifi_config.txt"
#define ENABLE_WPA_WRITE    0
#define WPA_PATH            "/etc/wpa_supplicant.conf"
#define LOG_WIFI_PATH       "/tmp/orch_wifi.log"
#define LOG_AP_PATH         "/tmp/orch_ap.log"
#define LOG_TINY_PATH       "/tmp/orch_tiny.log"
#define LOG_NFS_PATH        "/tmp/orch_nfs.log"
#define LOG_APP_PATH        "/tmp/orch_app.log"
#define BUTTON_LONG_MS      2000
#define RETRIGGER_GUARD_S   20
#define TIMEOUT_LISTEN_MS   15000
#define TIMEOUT_STA_MS      90000
#define TIMEOUT_NFS_MS      20000
#define TERM_GRACE_MS       300
#define AP_SUBNET_PREFIX    "192.168.4."
#define INTERNET_CHECK_HOST "8.8.8.8"
#define INTERNET_CHECK_PORT 53
#define INTERNET_CHECK_INTERVAL 60  // seconds
#endif
