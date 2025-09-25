#!/bin/sh
# enable_wifi.sh — Connect STA using /system/etc/device_wifi_config.txt
# BusyBox/ash friendly (Ingenic T23). Falls back to AP on failure.

set -u

CONFIG_FILE="/system/etc/device_wifi_config.txt"
WIFI_TOOL_DIR="/system/tools/wifi"
LOG="/tmp/wifi_debug.log"
AP_ENABLE="/system/www/ap_mode_enable.sh"
IFACE="wlan0"
WPA_CONF="/etc/wpa_supplicant.conf"
NFS_START="/system/start_nfs.sh"
APPLICATION="/system/nfs/keo-cam"
DEVICE_REG="/system/www/device_reg.sh"

WPA_SUPP="$WIFI_TOOL_DIR/wpa_supplicant"
WPA_CLI="$WIFI_TOOL_DIR/wpa_cli"

log() {
    echo "$1" >> "$LOG"
    echo "$1" > /dev/console
}


fail_to_ap() {
    log "[FAIL] $1"
    if [ -x "$AP_ENABLE" ]; then
        "$AP_ENABLE"
        log "[wifi_enable.sh] Restarting AP mode..."
    else
        log "[wifi_enable.sh] AP fallback script not found: $AP_ENABLE"
    fi
    exit 1
}

# ── Start
: > "$LOG" 2>/dev/null || true
log "====== Starting Wi-Fi setup ======"

# Basic checks
[ -x "$WPA_SUPP" ] || fail_to_ap "Missing $WPA_SUPP"
[ -x "$WPA_CLI" ]  || fail_to_ap "Missing $WPA_CLI"
[ -f "$CONFIG_FILE" ] || fail_to_ap "Config not found: $CONFIG_FILE"

# Load config
. "$CONFIG_FILE"
SSID="${ssid:-}"
WIFI_PSK="${wifi_psk:-}"
APP_USER="${username:-}"
APP_PASS="${password:-}"
CAMERA_ID="${camera_id:-}"
IS_REG="${is_reg:-0}"

[ -n "$SSID" ] || fail_to_ap "'ssid' missing in $CONFIG_FILE"

# Registration gate
if [ "$IS_REG" = "0" ]; then
    log "[WIFI] Device not registered, switching to AP mode..."
    exec "$AP_ENABLE"
fi

log "[WIFI] Loaded SSID=\"$SSID\" PSK=\"$WIFI_PSK\" camera_id=\"$CAMERA_ID\" user=\"$APP_USER\""

# ===== EXACT SEQUENCE =====
killall wpa_supplicant 2>>"$LOG" || true
killall udhcpc         2>>"$LOG" || true

rm -f /var/run/wpa_supplicant/"$IFACE" 2>>"$LOG" || true
mkdir -p /var/run/wpa_supplicant 2>>"$LOG" || true

ifconfig "$IFACE" up 2>>"$LOG" || true

"$WPA_SUPP" -D nl80211 -i "$IFACE" -c "$WPA_CONF" -B>/dev/console 2>&1 \
    || fail_to_ap "Unable to start wpa_supplicant (nl80211)"
sleep 1

NET_ID=$("$WPA_CLI" -i "$IFACE" add_network 2>>"$LOG")
NET_ID=$(printf '%s' "$NET_ID" | tr -cd '0-9')
[ -n "$NET_ID" ] || NET_ID=0
log "[WIFI] Using network ID: $NET_ID"

"$WPA_CLI" -i "$IFACE" set_network "$NET_ID" ssid "\"$SSID\""     >/dev/console 2>&1
[ -n "$WIFI_PSK" ] && \
"$WPA_CLI" -i "$IFACE" set_network "$NET_ID" psk  "\"$WIFI_PSK\"" >/dev/console 2>&1

"$WPA_CLI" -i "$IFACE" enable_network "$NET_ID">/dev/console 2>&1

udhcpc -i "$IFACE">/dev/console 2>&1 || log "[WIFI] udhcpc did not acquire a lease yet"
# ===== END SEQUENCE =====

log "[WIFI] Connected to \"$SSID\" successfully."

# Start NFS
if [ -x "$NFS_START" ]; then
    log "Starting NFS..."
    sh "$NFS_START"
else
    log "Error: $NFS_START not found or not executable"
fi

# Registration script
if [ -x "$DEVICE_REG" ]; then
    log "Running registration..."
    "$DEVICE_REG"
else
    log "Error: $DEVICE_REG not found or not executable"
fi

# Launch application
if [ -x "$APPLICATION" ]; then
    log "Running application..."
    "$APPLICATION" &
else
    log "Error: $APPLICATION not found or not executable"
fi

exit 0
