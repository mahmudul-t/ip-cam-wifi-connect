#!/bin/sh
# enable_wifi.sh — Fast Wi-Fi STA connect script for Ingenic T23

set -u

CONFIG_FILE="/system/etc/device_wifi_config.txt"
WIFI_TOOL_DIR="/system/tools/wifi"
AP_ENABLE="/system/www/ap_mode_enable.sh"
IFACE="wlan0"
WPA_CONF="/etc/wpa_supplicant.conf"
NFS_START="/system/start_nfs.sh"
# APPLICATION="/system/nfs/keo-cam"
#APPLICATION="/system/mmc_ext/keo-cam"

echo "====== Wi-Fi setup starting ======"

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

# mound sd card
mount /dev/mmcblk0p1 /system/mmc_ext/

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
    echo "[WIFI] Device not registered (is_reg=0). Going to AP mode..."
    exec "$AP_ENABLE"
fi

echo "[WIFI] Connecting to SSID: $SSID"

# Kill old clients
killall wpa_supplicant  || true
killall udhcpc          || true
usleep 100000 # 100ms



# Interface up
ifconfig "$IFACE" up || true

# Minimal wpa_supplicant.conf
if [ ! -f "$WPA_CONF" ]; then
    echo "[WIFI] Creating $WPA_CONF"
    cat > "$WPA_CONF" <<'CFG'
ctrl_interface=/var/run/wpa_supplicant
update_config=1
cfg80211_scan=1
CFG
fi

mkdir -p /var/run/wpa_supplicant

# Start wpa_supplicant

"$WIFI_TOOL_DIR/wpa_supplicant" -B -D nl80211 -i "$IFACE" -c "$WPA_CONF"  || 
{
    echo "[WIFI] nl80211 failed, trying wext..." 
    "$WIFI_TOOL_DIR/wpa_supplicant" -B -D wext -i "$IFACE" -c "$WPA_CONF"  \
        || fail_to_ap "Unable to start wpa_supplicant"
}

usleep 100000 # 100ms

# # Configure network quickly via wpa_cli
# "$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" remove_network all  || true

NET_ID=$("$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" add_network  || echo "")
NET_ID=$(printf '%s' "$NET_ID" | tr -cd '0-9')
[ -n "$NET_ID" ] || fail_to_ap "Failed to allocate WPA network ID"


# NET_ID=$(echo " network id is ... $NET_ID" | tr -cd '0-9')
# [ -n "$NET_ID" ] || fail_to_ap "Failed to allocate WPA network ID"

SSID_QUOTED="\"$SSID\""
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" set_network "$NET_ID" ssid "$SSID_QUOTED"

if [ -n "$WIFI_PSK" ]; then
    PSK_QUOTED="\"$WIFI_PSK\""
    "$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" set_network "$NET_ID" psk "$PSK_QUOTED" 
else
    echo "[WIFI] Empty PSK, assuming open network" > /dev/console
    "$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" set_network "$NET_ID" key_mgmt NONE  
fi

"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" enable_network "$NET_ID"  
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" select_network "$NET_ID"   
# "$WIFI_TOOL_DIR/wpa_cli" save_config 2>&1


echo "[WIFI] Waiting for link (max ~12s)..." 
MAX_RETRIES=60        # 60 * 200ms = 12s
i=1
LINK_OK=0

while [ $i -le $MAX_RETRIES ]; do
    STATUS=$("$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" status 2>/dev/null || true)
    WPA_STATE_LINE=$(printf '%s\n' "$STATUS" | grep '^wpa_state=' || true)
    WPA_STATE=${WPA_STATE_LINE#wpa_state=}

    if [ "$WPA_STATE" = "COMPLETED" ]; then
        LINK_OK=1
        break
    fi

    i=$((i+1))
    usleep 200000    # 200ms
done



if [ "$LINK_OK" -eq 1 ]; then
    echo "[WIFI] Link up. Running DHCP..." > /dev/console

    # DHCP (shorter timeout)
    udhcpc -i "$IFACE" -n -t 3 -T 2 >/dev/console 2>&1 || \
        echo "[WIFI] Warning: udhcpc could not get a lease yet" > /dev/console

    IP_ADDR=$(ifconfig "$IFACE" | awk '/inet addr/ {sub("addr:", "", $2); print $2}')
    echo "[WIFI] Connected. IP: ${IP_ADDR:-unknown}" > /dev/console
else
    echo "[WIFI] WARN: Wi-Fi link not established yet (state=$WPA_STATE)." > /dev/console
    echo "[WIFI] Staying in STA mode. wpa_supplicant will keep trying in background." > /dev/console
fi

echo "====== Wi-Fi setup finished ======" 

# # Start NFS in background (so Wi-Fi is "ready" faster)
# if [ -x "$NFS_START" ]; then
#     echo "[WIFI] Starting NFS in background..."
#     sh "$NFS_START"
# else
#     echo "[WIFI] NFS script not found or not executable: $NFS_START" 
# fi

# sleep 2

# # Start application
# if [ -x "$APPLICATION" ]; then
#     echo "[WIFI] Starting application: $APPLICATION" 
#     "$APPLICATION" &
# else
#     echo "[WIFI] Application not found or not executable: $APPLICATION" 
# fi


exit 0
