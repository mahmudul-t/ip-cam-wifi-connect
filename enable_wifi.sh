#!/bin/sh
# wifi_enable.sh — Connect STA using /system/etc/device_wifi_config.txt
# BusyBox/ash friendly (Ingenic T23). Falls back to AP on failure.

set -u

CONFIG_FILE="/system/etc/device_wifi_config.txt"
WIFI_TOOL_DIR="/system/tools/wifi"
LOG="/tmp/wifi_debug.log"
AP_ENABLE="/system/www/ap_mode_enable.sh"
IFACE="wlan0"
WPA_CONF="/etc/wpa_supplicant.conf"

log() {
    echo "$1" >> "$LOG"
    echo "$1" > /dev/console
}

mask() {
    v="${1:-}"
    [ -z "$v" ] && { echo ""; return; }
    len=${#v}
    if [ "$len" -le 4 ]; then printf '****'
    else
        first=$(printf '%s' "$v")
        last=$(printf '%s' "$v")
        printf '%s***%s' "$first" "$last"
    fi
}

fail_to_ap() {
    log "[FAIL] $1"
    if [ -x "$AP_ENABLE" ]; then
        "$AP_ENABLE"
        echo "[wifi_enable.sh] ... Failed. Restarting AP mode..." > /dev/console
    else
        echo "[wifi_enable.sh] ... Failed. AP fallback script not found: $AP_ENABLE" > /dev/console
    fi
    exit 1
}

# ── Start
: > "$LOG" 2>/dev/null || true
log "====== Starting Wi-Fi setup ======"

# 0) Basic checks
[ -x "$WIFI_TOOL_DIR/wpa_supplicant" ] || fail_to_ap "Missing $WIFI_TOOL_DIR/wpa_supplicant"
[ -x "$WIFI_TOOL_DIR/wpa_cli" ]        || fail_to_ap "Missing $WIFI_TOOL_DIR/wpa_cli"
[ -f "$CONFIG_FILE" ]                  || fail_to_ap "Wi-Fi config not found at $CONFIG_FILE"

# 1) Load config
. "$CONFIG_FILE"

SSID="${ssid:-}"
WIFI_PSK="${wifi_psk:-}"
APP_USER="${username:-}"
APP_PASS="${password:-}"
CAMERA_ID="${camera_id:-}"
IS_REG="${is_reg:-0}" 
HIDDEN="${hidden:-0}"
COUNTRY="${country:-}"    # optional e.g., BD/US/GB

# # 1.5) Registration check
# if [ "$IS_REG" = "0" ]; then
#     log "[WIFI] Device not registered, switching to AP mode..."
#     exec "$AP_ENABLE"
#     exit 0
# fi


[ -n "$SSID" ] || fail_to_ap "'ssid' missing in $CONFIG_FILE"

log "[WIFI] Loaded SSID=\"$SSID\" PSK=\"$(mask "$WIFI_PSK")\" camera_id=\"$CAMERA_ID\" user=\"$APP_USER\""

# # 2) Optional: regulatory domain
# if [ -n "${COUNTRY}" ]; then
#     iw reg set "$COUNTRY" 2>>"$LOG" || log "[WIFI] iw reg set $COUNTRY failed"
# fi

# 3) Kill old clients
killall wpa_supplicant 2>>"$LOG" || true
killall udhcpc         2>>"$LOG" || true
sleep 1

# 4) Bring up interface
ifconfig "$IFACE" up 2>>"$LOG" || true

# 5) Ensure ctrl dir
mkdir -p /var/run/wpa_supplicant

# 6) Ensure minimal wpa_supplicant.conf
if [ ! -f "$WPA_CONF" ]; then
    log "[WIFI] Creating minimal $WPA_CONF"
    cat > "$WPA_CONF" <<'CFG'
ctrl_interface=/var/run/wpa_supplicant
update_config=1
cfg80211_scan=1
CFG
fi

# 7) Start wpa_supplicant
"$WIFI_TOOL_DIR/wpa_supplicant" -B -D nl80211 -i "$IFACE" -c "$WPA_CONF" >>"$LOG" 2>&1 || {
    log "[WIFI] nl80211 failed, retrying with wext"
    "$WIFI_TOOL_DIR/wpa_supplicant" -B -D wext -i "$IFACE" -c "$WPA_CONF" >>"$LOG" 2>&1 \
        || fail_to_ap "Unable to start wpa_supplicant"
}
sleep 1

# 8) Reset networks
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" remove_network all >>"$LOG" 2>&1 || true

# 9) Add and configure network
NET_ID=$("$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" add_network 2>>"$LOG")
NET_ID=$(printf '%s' "$NET_ID" | tr -cd '0-9')
[ -n "$NET_ID" ] || fail_to_ap "Failed to allocate WPA network ID"
log "[WIFI] Using network ID: $NET_ID"

SSID_QUOTED="\"$SSID\""
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" set_network "$NET_ID" ssid "$SSID_QUOTED" >>"$LOG" 2>&1

if [ -n "$WIFI_PSK" ]; then
    PSK_QUOTED="\"$WIFI_PSK\""
    "$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" set_network "$NET_ID" psk "$PSK_QUOTED" >>"$LOG" 2>&1
fi

# Hidden SSID support (only if requested)
if [ "${HIDDEN}" = "1" ]; then
    "$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" set_network "$NET_ID" scan_ssid 1 >>"$LOG" 2>&1
    log "[WIFI] Hidden SSID mode enabled (scan_ssid=1)"
fi

# 10) Enable/select and reassociate
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" enable_network "$NET_ID"   >>"$LOG" 2>&1
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" select_network "$NET_ID"   >>"$LOG" 2>&1
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" save_config                >>"$LOG" 2>&1
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" reassociate                >>"$LOG" 2>&1

# 11) Wait for association (link only)
log "[WIFI] Waiting for Wi-Fi association…"
MAX_RETRIES=15
LINK_OK=0
i=1
while [ $i -le $MAX_RETRIES ]; do
    STATUS=$("$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" status 2>>"$LOG")
    WPA_STATE=$(echo "$STATUS" | awk -F= '/^wpa_state=/{print $2}')
    SSID_CUR=$(echo "$STATUS" | awk -F= '/^ssid=/{print $2}')
    log "[WIFI] Try $i/$MAX_RETRIES: WPA_STATE=$WPA_STATE SSID=${SSID_CUR:-none}"
    if [ "$WPA_STATE" = "COMPLETED" ]; then
        LINK_OK=1; break
    fi
    sleep 1; i=$((i+1))
done

[ $LINK_OK -eq 1 ] || fail_to_ap "Link not established after ${MAX_RETRIES}s (state=$WPA_STATE)"

log "[WIFI] Associated to \"$SSID\". Starting DHCP…"

# 12) DHCP
udhcpc -i "$IFACE" -n -t 5 -T 3 >>"$LOG" 2>&1 || log "[WIFI] udhcpc did not acquire a lease yet"

log "[WIFI] connect to \"$SSID\" is succesfull...."
# # 13) Final IP
# FINAL_IP=$(ip -4 addr show "$IFACE" | awk '/inet /{print $2}' | awk -F/ '{print $1}' | head -n1)

# if [ -n "$FINAL_IP" ]; then
#     echo "[wifi_enable.sh] Connected with IP: $FINAL_IP" > /dev/console
#     log "[WIFI] Final IP: $FINAL_IP"
# else
#     echo "[wifi_enable.sh] Associated to \"$SSID\" but no DHCP lease yet" > /dev/console
#     log "[WIFI] Associated but no DHCP lease."
# fi

exit 0
