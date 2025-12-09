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

if [ -z "$COTURN_PORT" ]; then
  echo "Error: COTURN_PORT must be set."
  echo "Example: docker run -e COTURN_PORT=3478 crossdesk-server"
  exit 1
fi

if [ -z "$MIN_PORT" ] || [ -z "$MAX_PORT" ]; then
  echo "Error: MIN_PORT and MAX_PORT must be set."
  echo "Example: docker run -e MIN_PORT=50000 -e MAX_PORT=60000 crossdesk-server"
  exit 1
fi

# check and generate certificates if needed
CERT_DIR="/var/lib/crossdesk/certs"
CERT_KEY="$CERT_DIR/crossdesk.cn.key"
CERT_BUNDLE="$CERT_DIR/crossdesk.cn_bundle.crt"
CERT_ROOT="$CERT_DIR/crossdesk.cn_root.crt"

if [ ! -f "$CERT_KEY" ] || [ ! -f "$CERT_BUNDLE" ]; then
  echo "Certificate files not found, generating certificates..."
  mkdir -p "$CERT_DIR"
  
  # Run generate_certs.sh with EXTERNAL_IP and output directory
  bash /docker/generate_certs.sh "$EXTERNAL_IP" "$CERT_DIR"
  
  # Verify certificates were generated
  if [ ! -f "$CERT_KEY" ] || [ ! -f "$CERT_BUNDLE" ] || [ ! -f "$CERT_ROOT" ]; then
    echo "Error: Failed to generate certificate files"
    exit 1
  fi
  
  echo "Certificates generated successfully"
else
  echo "Certificate files found, skipping generation"
fi

# generate coturn configuration file
mkdir -p /etc/coturn
cat > "$CONF_FILE" <<EOF
# coturn auto-generated configuration
listening-port=${COTURN_PORT}
listening-ip=${INTERNAL_IP}
external-ip=${EXTERNAL_IP}
min-port=${MIN_PORT}
max-port=${MAX_PORT}
verbose
fingerprint
lt-cred-mech
user=crossdesk:crossdeskpw
realm=crossdesk
cert=${CERT_FILE}
pkey=${PKEY_FILE}
log-file=/var/log/crossdesk/turn.log
no-cli
EOF

echo "generated coturn config at $CONF_FILE"
echo "using certificate: $CERT_FILE"

# start coturn in the background
exec turnserver -c "$CONF_FILE" &

# start crossdesk-server as main foreground process
echo "Starting crossdesk-server..."
echo "Certificate directory: $CERT_DIR"
echo "Certificate files:"
ls -la "$CERT_DIR" || echo "Warning: Cannot list certificate directory"

exec ./crossdesk-server/crossdesk_server ${CROSSDESK_SERVER_PORT}