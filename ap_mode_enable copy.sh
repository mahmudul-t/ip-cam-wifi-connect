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
trap "rm -f $LOCK_FILE" EXIT

# Load retry count
if [ -f "$RETRY_FILE" ]; then
    RETRY_COUNT=$(cat "$RETRY_FILE")
else
    RETRY_COUNT=0
fi

echo "[BOOT BTN] Long press detected, starting AP mode" > /dev/console

# Kill old services and reset interface
killall udhcpd hostapd httpd udhcpc wpa_supplicant > /dev/console
rm -f /var/run/wpa_supplicant/wlan0
sleep 1
#ifconfig wlan0 down
ifconfig wlan0 up
/system/tools/wifi/wpa_supplicant -D nl80211 -i wlan0 -c /etc/wpa_supplicant.conf -B

sleep 1

# === Step 1: Start hostapd ===
ifconfig wlan0 up
/system/tools/wifi/hostapd -B /etc/hostapd.conf
sleep 1
if ! pidof hostapd >/dev/null; then
    echo "[AP]ERROR: hostapd failed to start" > /dev/console
    goto_retry
fi
echo "[AP]hostapd started successfully" > /dev/console

# === Step 2: Assign static IP ===
#ip addr flush dev wlan0
ip addr add 192.168.4.1/24 dev wlan0
sleep 1
if ! ip addr show wlan0 | grep -q "192.168.4.1"; then
    echo "[AP]ERROR: Failed to assign IP address" > /dev/console
    goto_retry
fi
echo "[AP]IP address assigned to wlan0" > /dev/console

# === Step 3: Start DHCP ===
mkdir -p /var/lib/misc > /dev/console
touch /var/lib/misc/udhcpd.leases > /dev/console
udhcpd /system/tools/wifi/udhcpd.conf > /dev/console
sleep 1
if ! pidof udhcpd >/dev/null; then
    echo "[AP]ERROR: udhcpd failed to start" > /dev/console
    goto_retry
fi
echo "[AP]DHCP server started" > /dev/console

# === Step 4: Start HTTP server ===
cd /system/
httpd -p 80 -h ./www/
sleep 1
if ! pidof httpd >/dev/null; then
    echo "[AP]ERROR: httpd failed to start" > /dev/console
    goto_retry
fi
echo "[AP]Web server running at http://192.168.4.1/" > /dev/console

# === Success ===
echo "[AP]All AP components started successfully. Clearing retry count." > /dev/console
rm -f "$RETRY_FILE"
echo "tiny server started ..............>>>>"
/system/www/tiny_server
exit 0

# === Retry function ===
goto_retry() {
    RETRY_COUNT=$((RETRY_COUNT + 1))
    echo "$RETRY_COUNT" > "$RETRY_FILE"

    if [ "$RETRY_COUNT" -ge "$MAX_RETRIES" ]; then
        echo "[AP]FATAL: Failed $MAX_RETRIES times. Giving up." > /dev/console
        rm -f "$RETRY_FILE"
        exit 1
    else
        echo "[AP]Retrying AP mode in 5 seconds... (Attempt $RETRY_COUNT of $MAX_RETRIES)" > e
        sleep 5
        exec /system/mmc_ext/bin/ap_mode_enable.sh
    fi
}

