#!/bin/bash
set -e

DEFAULT_IP="192.168.4.1"
KEY_SIZE=2048
DAYS=3650
ROOT_SUBJ="/CN=Orchestrator Local Root CA"
V3_EXT="v3.ext"

CERTS_DIR=$(cd "$(dirname "$0")" && pwd)
ESP32_IP=${1:-$DEFAULT_IP}

openssl genrsa -out "$CERTS_DIR/rootCA.key" $KEY_SIZE
openssl req -x509 -new -key "$CERTS_DIR/rootCA.key" -days $DAYS -subj "$ROOT_SUBJ" -out "$CERTS_DIR/rootCA.pem"

openssl genrsa -out "$CERTS_DIR/prvtkey.pem" $KEY_SIZE
openssl req -new -key "$CERTS_DIR/prvtkey.pem" -subj "/CN=$ESP32_IP" -out "$CERTS_DIR/esp32.csr"

cat > "$CERTS_DIR/$V3_EXT" <<EOF
authorityKeyIdentifier=keyid,issuer
basicConstraints=CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = IP:$ESP32_IP
EOF

openssl x509 -req -in "$CERTS_DIR/esp32.csr" -CA "$CERTS_DIR/rootCA.pem" -CAkey "$CERTS_DIR/rootCA.key" -CAcreateserial -out "$CERTS_DIR/servercert.pem" -days $DAYS -sha256 -extfile "$CERTS_DIR/$V3_EXT"

rm -f "$CERTS_DIR/esp32.csr" "$CERTS_DIR/$V3_EXT"

echo "rootCA.pem      - trust anchor"
echo "servercert.pem  - device cert"
echo "prvtkey.pem     - device private key"
