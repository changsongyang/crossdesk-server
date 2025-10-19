#!/bin/bash
set -e

# environment variables for coturn
CONF_FILE=/etc/coturn/turnserver.conf
CERT_FILE=/opt/turnserver/turn_server_cert.pem
PKEY_FILE=/opt/turnserver/turn_server_pkey.pem

# environment variables for crossdesk-server
CROSSDESK_SERVER_PORT=${CROSSDESK_SERVER_PORT:-9090}

# check environment variables
if [ -z "$EXTERNAL_IP" ] || [ -z "$INTERNAL_IP" ]; then
  echo "Error: EXTERNAL_IP and INTERNAL_IP must be set."
  echo "Example: docker run -e EXTERNAL_IP=1.2.3.4 -e INTERNAL_IP=10.0.0.5 crossdesk-server"
  exit 1
fi

# generate coturn configuration file
mkdir -p /etc/coturn
cat > "$CONF_FILE" <<EOF
# coturn auto-generated configuration
listening-port=3478
listening-ip=${INTERNAL_IP}
external-ip=${EXTERNAL_IP}
min-port=30000
max-port=60000
verbose
fingerprint
lt-cred-mech
user=crossdesk:crossdeskpw
realm=crossdesk
cert=${CERT_FILE}
pkey=${PKEY_FILE}
log-file=/crossdesk-server/logs/turn.log
no-cli
EOF

echo "generated coturn config at $CONF_FILE"
echo "using certificate: $CERT_FILE"

# start coturn in the background
exec turnserver -c "$CONF_FILE" &

# start crossdesk-server as main foreground process
echo "Starting crossdesk-server..."
./crossdesk-server/crossdesk_server \
    ${CROSSDESK_SERVER_PORT} \
    /crossdesk-server/certs/ \
    /crossdesk-server/db/crossdesk_server.db \
    /crossdesk-server/logs