#!/bin/sh
# Switch to AP mode (no STA). BusyBox ash compatible.

set -eu
PATH=/sbin:/bin:/usr/sbin:/usr/bin:/system/tools/wifi:/system/www
WLAN_IF="wlan0"
AP_IP="192.168.4.1/24"
LOCK_FILE="/tmp/ap_mode.lock"
RETRY_FILE="/tmp/ap_mode.retry"
MAX_RETRIES=3
TINY_PORT=8080

goto_retry() {
  RETRY_COUNT=$((RETRY_COUNT + 1))
  echo "$RETRY_COUNT" > "$RETRY_FILE"
  if [ "$RETRY_COUNT" -ge "$MAX_RETRIES" ]; then
    echo "[AP] FATAL: failed $MAX_RETRIES times" > /dev/console
    rm -f "$RETRY_FILE"
    exit 1
  fi
  echo "[AP] retrying in 5s... ($RETRY_COUNT/$MAX_RETRIES)" > /dev/console
  sleep 5
  rm -f "$LOCK_FILE"
  exec "$0"
}

kill_port_8080_if_busy() {
  pidof tiny_server >/dev/null 2>&1 && killall tiny_server 2>/dev/null || true
  pidof httpd >/dev/null 2>&1 && killall httpd 2>/dev/null || true
  # hard kill any 8080 listener
  (netstat -anp 2>/dev/null | awk '/:8080 .*LISTEN/ {print $7}' | cut -d/ -f1 | xargs -r kill -9) 2>/dev/null || true
}

[ -d /tmp ] || mkdir -p /tmp
if [ -f "$LOCK_FILE" ]; then
  echo "[AP] another instance running; exit" > /dev/console
  exit 0
fi
echo $$ > "$LOCK_FILE"
trap 'rm -f "$LOCK_FILE"' EXIT

RETRY_COUNT=0
[ -f "$RETRY_FILE" ] && RETRY_COUNT=$(cat "$RETRY_FILE" 2>/dev/null || echo 0)

echo "[AP] switching to AP mode" > /dev/console

# Kill STA + AP stack
killall udhcpc udhcpd hostapd httpd wpa_supplicant tiny_server 2>/dev/null || true
rm -f /var/run/wpa_supplicant/"$WLAN_IF" 2>/dev/null || true

# Reset iface for AP
ip link set "$WLAN_IF" down || ifconfig "$WLAN_IF" down || true
sleep 1
command -v iw >/dev/null 2>&1 && iw dev "$WLAN_IF" set type __ap 2>/dev/null || true
ip link set "$WLAN_IF" up || ifconfig "$WLAN_IF" up

# hostapd
/system/tools/wifi/hostapd -B /etc/hostapd.conf
sleep 1
pidof hostapd >/dev/null || { echo "[AP] hostapd failed" > /dev/console; goto_retry; }
echo "[AP] hostapd OK" > /dev/console

# IP
ip addr flush dev "$WLAN_IF"
ip addr add "$AP_IP" dev "$WLAN_IF"
sleep 1
ip addr show "$WLAN_IF" | grep -q "${AP_IP%/*}" || { echo "[AP] IP set failed" > /dev/console; goto_retry; }
echo "[AP] IP ${AP_IP%/*} OK" > /dev/console

# DHCP
mkdir -p /var/lib/misc
: > /var/lib/misc/udhcpd.leases
udhcpd /system/tools/wifi/udhcpd.conf
sleep 1
pidof udhcpd >/dev/null || { echo "[AP] udhcpd failed" > /dev/console; goto_retry; }
echo "[AP] DHCP OK" > /dev/console

# onboard server
kill_port_8080_if_busy
echo "[AP] tiny_server starting on :$TINY_PORT" > /dev/console
/system/www/tiny_server &
echo "[AP] tiny_server pid=$!" > /dev/console

# success
rm -f "$RETRY_FILE"
echo "[AP] AP stack up" > /dev/console
exit 0
