#!/usr/bin/env bash
# Rezip this directory and push it to the Azure App Service.
# Run from anywhere -- it cd's to its own location first.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

ZIP_PATH="/tmp/gbdt-deploy.zip"
rm -f "$ZIP_PATH"
zip -rq "$ZIP_PATH" . -x ".env" -x ".env.example" -x "__pycache__/*" -x "*.pyc" -x ".DS_Store" -x "deploy.sh"

az webapp deploy \
  --name gbdt-gtfs \
  --resource-group gtfs_group \
  --src-path "$ZIP_PATH" \
  --type zip

echo "Deployed. https://gbdt-gtfs-aqfbgfcxhbcdcfhz.canadacentral-01.azurewebsites.net"
