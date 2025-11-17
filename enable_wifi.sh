#!/bin/sh
# enable_wifi.sh — Fast Wi-Fi STA connect script for Ingenic T23

set -u

CONFIG_FILE="/system/etc/device_wifi_config.txt"
WIFI_TOOL_DIR="/system/tools/wifi"
AP_ENABLE="/system/www/ap_mode_enable.sh"
IFACE="wlan0"
WPA_CONF="/etc/wpa_supplicant.conf"
NFS_START="/system/start_nfs.sh"
APPLICATION="/system/nfs/keo-cam"

echo "====== Wi-Fi setup (FAST) starting ======" > /dev/console

fail_to_ap() {
    echo "[WIFI] ERROR: $1" > /dev/console
    if [ -x "$AP_ENABLE" ]; then
        echo "[WIFI] Switching to AP mode..." > /dev/console
        exec "$AP_ENABLE"
    else
        echo "[WIFI] AP fallback script not found: $AP_ENABLE" > /dev/console
        exit 1
    fi
}

# Basic checks
[ -x "$WIFI_TOOL_DIR/wpa_supplicant" ] || fail_to_ap "Missing wpa_supplicant"
[ -x "$WIFI_TOOL_DIR/wpa_cli" ]        || fail_to_ap "Missing wpa_cli"
[ -f "$CONFIG_FILE" ]                  || fail_to_ap "Config not found: $CONFIG_FILE"

# Load config
. "$CONFIG_FILE"

SSID="${ssid:-}"
WIFI_PSK="${wifi_psk:-}"
IS_REG="${is_reg:-0}"

[ -n "$SSID" ] || fail_to_ap "'ssid' missing in $CONFIG_FILE"

# Registration check
if [ "$IS_REG" = "0" ]; then
    echo "[WIFI] Device not registered (is_reg=0). Going to AP mode..." > /dev/console
    exec "$AP_ENABLE"
fi

echo "[WIFI] Connecting to SSID: $SSID" > /dev/console

# Kill old clients
killall wpa_supplicant 2>/dev/null || true
killall udhcpc         2>/dev/null || true
sleep 1

# Interface up
ifconfig "$IFACE" up 2>/dev/null || true

# Minimal wpa_supplicant.conf
if [ ! -f "$WPA_CONF" ]; then
    echo "[WIFI] Creating $WPA_CONF" > /dev/console
    cat > "$WPA_CONF" <<'CFG'
ctrl_interface=/var/run/wpa_supplicant
update_config=1
cfg80211_scan=1
CFG
fi

mkdir -p /var/run/wpa_supplicant

# Start wpa_supplicant
echo "[WIFI] Starting wpa_supplicant..." > /dev/console
"$WIFI_TOOL_DIR/wpa_supplicant" -B -D nl80211 -i "$IFACE" -c "$WPA_CONF" 2>/dev/console || {
    echo "[WIFI] nl80211 failed, trying wext..." > /dev/console
    "$WIFI_TOOL_DIR/wpa_supplicant" -B -D wext -i "$IFACE" -c "$WPA_CONF" 2>/dev/console \
        || fail_to_ap "Unable to start wpa_supplicant"
}
sleep 1

# Configure network quickly via wpa_cli
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" remove_network all >/dev/null 2>&1 || true

NET_ID=$("$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" add_network 2>/dev/null)
NET_ID=$(echo "$NET_ID" | tr -cd '0-9')
[ -n "$NET_ID" ] || fail_to_ap "Failed to allocate WPA network ID"

SSID_QUOTED="\"$SSID\""
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" set_network "$NET_ID" ssid "$SSID_QUOTED" >/dev/null 2>&1

if [ -n "$WIFI_PSK" ]; then
    PSK_QUOTED="\"$WIFI_PSK\""
    "$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" set_network "$NET_ID" psk "$PSK_QUOTED" >/dev/null 2>&1
else
    echo "[WIFI] Empty PSK, assuming open network" > /dev/console
    "$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" set_network "$NET_ID" key_mgmt NONE >/dev/null 2>&1
fi

"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" enable_network "$NET_ID"  >/dev/null 2>&1
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" select_network "$NET_ID"  >/dev/null 2>&1
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" save_config                >/dev/null 2>&1

# Wait for association (short & quiet)
echo "[WIFI] Waiting for link (max ~8s)..." > /dev/console
MAX_RETRIES=8
i=1
LINK_OK=0

while [ $i -le $MAX_RETRIES ]; do
    STATUS=$("$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" status 2>/dev/null)
    WPA_STATE=$(echo "$STATUS" | grep '^wpa_state=' | cut -d= -f2)

    if [ "$WPA_STATE" = "COMPLETED" ]; then
        LINK_OK=1
        break
    fi

    i=$((i+1))
    sleep 1
done

[ "$LINK_OK" -eq 1 ] || fail_to_ap "Link not established (state=$WPA_STATE)"

echo "[WIFI] Link up. Running DHCP..." > /dev/console

# DHCP (shorter timeout)
udhcpc -i "$IFACE" -n -t 3 -T 2 >/dev/console 2>&1 || \
    echo "[WIFI] Warning: udhcpc could not get a lease yet" > /dev/console

IP_ADDR=$(ifconfig "$IFACE" 2>/dev/null | awk '/inet addr/ {sub("addr:", "", $2); print $2}')
echo "[WIFI] Connected. IP: ${IP_ADDR:-unknown}" > /dev/console

# Start NFS in background (so Wi-Fi is "ready" faster)
if [ -x "$NFS_START" ]; then
    echo "[WIFI] Starting NFS in background..." > /dev/console
    sh "$NFS_START" &
else
    echo "[WIFI] NFS script not found or not executable: $NFS_START" > /dev/console
fi

# Start application
if [ -x "$APPLICATION" ]; then
    echo "[WIFI] Starting application: $APPLICATION" > /dev/console
    "$APPLICATION" &
else
    echo "[WIFI] Application not found or not executable: $APPLICATION" > /dev/console
fi

echo "====== Wi-Fi setup finished ======" > /dev/console
exit 0
