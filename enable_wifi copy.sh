#!/bin/sh
# wifi_enable.sh — Connect STA using /system/etc/device_wifi_config.txt
# Works with BusyBox ash on Ingenic T23 images.

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
    if [ "$len" -le 4 ]; then
        printf '****'
    else
        first=$(printf '%s' "$v" | cut -c1)
        last=$(printf '%s' "$v" | tail -c1)
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

log ""
log "====== Starting Wi-Fi setup ======"
echo "" > "$LOG" 2>/dev/null || true

# 0) Basic checks
[ -x "$WIFI_TOOL_DIR/wpa_supplicant" ] || fail_to_ap "Missing $WIFI_TOOL_DIR/wpa_supplicant"
[ -x "$WIFI_TOOL_DIR/wpa_cli" ]        || fail_to_ap "Missing $WIFI_TOOL_DIR/wpa_cli"
[ -f "$CONFIG_FILE" ]                  || fail_to_ap "Wi-Fi config not found at $CONFIG_FILE"

# 1) Load config (expects lowercase keys: ssid, wifi_psk, username, password, camera_id)
# shellcheck disable=SC1090
. "$CONFIG_FILE"

SSID="${ssid:-}"
WIFI_PSK="${wifi_psk:-}"
APP_USER="${username:-}"
APP_PASS="${password:-}"
CAMERA_ID="${camera_id:-}"

[ -n "$SSID" ] || fail_to_ap "'ssid' missing in $CONFIG_FILE"

log "[INFO] Loaded SSID=\"$SSID\" Wi-Fi PSK=\"$(mask "$WIFI_PSK")\" camera_id=\"$CAMERA_ID\" user=\"$APP_USER\""

# 2) Ensure wpa_supplicant.conf exists (minimal)
if [ ! -f "$WPA_CONF" ]; then
    log "[INFO] Creating minimal $WPA_CONF"
    cat > "$WPA_CONF" <<'CFG'
ctrl_interface=/var/run/wpa_supplicant
update_config=1
cfg80211_scan=1
CFG
fi

# 3) Clean previous clients
killall wpa_supplicant 2>>"$LOG" || true
killall udhcpc         2>>"$LOG" || true
sleep 1

# 4) Bring up interface
ifconfig "$IFACE" up 2>>"$LOG" || true

# 5) Ensure control dir
mkdir -p /var/run/wpa_supplicant

# 6) Start wpa_supplicant as a daemon (-B). Try nl80211 then wext.
"$WIFI_TOOL_DIR/wpa_supplicant" -B -D nl80211 -i "$IFACE" -c "$WPA_CONF" >>"$LOG" 2>&1 || {
    log "[WARN] nl80211 failed, retrying with wext"
    "$WIFI_TOOL_DIR/wpa_supplicant" -B -D wext -i "$IFACE" -c "$WPA_CONF" >>"$LOG" 2>&1 || fail_to_ap "Unable to start wpa_supplicant"
}
sleep 1

# 7) Reset networks
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" remove_network all >>"$LOG" 2>&1 || true

# 8) Add network
NET_ID=$("$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" add_network 2>>"$LOG")
NET_ID=$(printf '%s' "$NET_ID" | tr -cd '0-9')
[ -n "$NET_ID" ] || fail_to_ap "Failed to allocate WPA network ID"
log "[INFO] Using network ID: $NET_ID"

# 9) Configure SSID and key
SSID_QUOTED="\"$SSID\""
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" set_network "$NET_ID" ssid "$SSID_QUOTED" >>"$LOG" 2>&1

if [ -n "$WIFI_PSK" ]; then
    PSK_QUOTED="\"$WIFI_PSK\""
    "$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" set_network "$NET_ID" psk "$PSK_QUOTED" >>"$LOG" 2>&1
else
    "$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" set_network "$NET_ID" key_mgmt NONE >>"$LOG" 2>&1
fi

# Hidden SSIDs often need active scan
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" set_network "$NET_ID" scan_ssid 1 >>"$LOG" 2>&1 || true

# 10) Enable/select and reassociate
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" enable_network "$NET_ID"   >>"$LOG" 2>&1
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" select_network "$NET_ID"   >>"$LOG" 2>&1
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" save_config                >>"$LOG" 2>&1
"$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" reassociate                >>"$LOG" 2>&1

# 11) Wait for association (up to ~10s). We don't require IP yet.
log "[INFO] Waiting for Wi-Fi association…"
MAX_RETRIES=10
LINK_OK=0
i=1
while [ $i -le $MAX_RETRIES ]; do
    STATUS=$("$WIFI_TOOL_DIR/wpa_cli" -i "$IFACE" status 2>>"$LOG")
    WPA_STATE=$(echo "$STATUS" | awk -F= '/^wpa_state=/{print $2}')
    SSID_CUR=$(echo "$STATUS" | awk -F= '/^ssid=/{print $2}')
    log "[DEBUG] Try $i/$MAX_RETRIES: WPA_STATE=$WPA_STATE SSID=${SSID_CUR:-none}"
    if [ "$WPA_STATE" = "COMPLETED" ]; then
        LINK_OK=1
        break
    fi
    sleep 1
    i=$((i+1))
done

[ $LINK_OK -eq 1 ] || fail_to_ap "Link not established after ${MAX_RETRIES}s (state=$WPA_STATE)"

log "[SUCCESS] Associated to \"$SSID\". Starting DHCP…"

# 12) DHCP (some networks hand IP via wpa_supplicant, but we ensure with udhcpc)
udhcpc -i "$IFACE" -n -t 5 -T 3 >>"$LOG" 2>&1 || log "[WARN] udhcpc did not acquire a lease yet"

# 13) Final IP report
FINAL_IP=$(ip -4 addr show "$IFACE" | awk '/inet /{print $2}' | awk -F/ '{print $1}' | head -n1)

if [ -n "$FINAL_IP" ]; then
    echo "[wifi_enable.sh] Connected with IP: $FINAL_IP" > /dev/console
    log "[INFO] Final IP: $FINAL_IP"
else
    echo "[wifi_enable.sh] Associated to \"$SSID\" but no DHCP lease yet" > /dev/console
    log "[WARN] Associated but no DHCP lease."
fi

exit 0
