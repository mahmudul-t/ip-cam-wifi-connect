#!/bin/sh
LOCK_FILE="/tmp/ap_mode.lock"
RETRY_FILE="/tmp/ap_mode.retry"
MAX_RETRIES=3

# Prevent multiple instances
if [ -f "$LOCK_FILE" ]; then
    echo "[AP] Another instance is already running. Exiting..." > /dev/console
    exit 0
fi
touch "$LOCK_FILE"
trap 'rm -f "$LOCK_FILE"' EXIT

# Load retry count
RETRY_COUNT=0
[ -f "$RETRY_FILE" ] && RETRY_COUNT=$(cat "$RETRY_FILE")

echo "starting AP mode" > /dev/console

# Clean previous services
killall udhcpd hostapd httpd udhcpc wpa_supplicant 2>/dev/null
rm -f /var/run/wpa_supplicant/wlan0

# Bring iface up (no wpa_supplicant in AP mode)
ifconfig wlan0 up

# === Step 1: hostapd ===
/system/tools/wifi/hostapd -B /etc/hostapd.conf
sleep 1
if ! pidof hostapd >/dev/null; then
    echo "[AP] ERROR: hostapd failed to start" > /dev/console
    goto_retry
fi
echo "[AP] hostapd started successfully" > /dev/console

# === Step 2: IP ===
ip addr flush dev wlan0
ip addr add 192.168.4.1/24 dev wlan0
sleep 1
if ! ip addr show wlan0 | grep -q "192.168.4.1"; then
    echo "[AP] ERROR: Failed to assign IP address" > /dev/console
    goto_retry
fi
echo "[AP] IP address assigned to wlan0" > /dev/console

# === Step 3: DHCP ===
mkdir -p /var/lib/misc
: > /var/lib/misc/udhcpd.leases
udhcpd /system/tools/wifi/udhcpd.conf
sleep 1
if ! pidof udhcpd >/dev/null; then
    echo "[AP] ERROR: udhcpd failed to start" > /dev/console
    goto_retry
fi
echo "[AP] DHCP server started" > /dev/console

# === Step 4: HTTP ===
# cd /system/
# httpd -p 80 -h ./www/
# sleep 1
# if ! pidof httpd >/dev/null; then
#     echo "[AP] ERROR: httpd failed to start" > /dev/console
#     goto_retry
# fi
# echo "[AP] Web server at http://192.168.4.1/" > /dev/console

# === Success ===
echo "[AP] All AP components started. Clearing retry count." > /dev/console
rm -f "$RETRY_FILE"

# Start tiny_server (background) and exit
echo "[AP] tiny_server starting..." > /dev/console
/system/www/tiny_server &
echo "[AP] tiny_server pid=$!" > /dev/console
exit 0

# === Retry function ===
goto_retry() {
    RETRY_COUNT=$((RETRY_COUNT + 1))
    echo "$RETRY_COUNT" > "$RETRY_FILE"

    if [ "$RETRY_COUNT" -ge "$MAX_RETRIES" ]; then
        echo "[AP] FATAL: Failed $MAX_RETRIES times. Giving up." > /dev/console
        rm -f "$RETRY_FILE"
        exit 1
    else
        echo "[AP] Retrying AP mode in 5 seconds... (Attempt $RETRY_COUNT of $MAX_RETRIES)" > /dev/console
        sleep 5
        rm -f "$LOCK_FILE"    # important: unlock before re-exec
        exec "$0"             # re-exec the same script
    fi
}
