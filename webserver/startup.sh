#!/bin/bash
# Run at login by launchd (see com.jettmu.gtfs-startup.plist and NGINX.md).
# Brings up the web stack, then reissues the TLS cert if it expires within 2 weeks.
# Safe to re-run: restarts gunicorn, reloads nginx, leaves a running tunnel alone.

set -u
export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"

REPO="$(cd "$(dirname "$0")/.." && pwd)"
TUNNEL_ID="0a21e076-0453-4845-be0a-f15ef0f7ad5d"
DOMAIN="gtfs.jettm.dev"
LE_DIR="$HOME/letsencrypt"
LOG_DIR="$HOME/Library/Logs"
RENEW_WITHIN_SECONDS=$((14 * 24 * 60 * 60))
GUNICORN_PID=/tmp/gtfs-gunicorn.pid

status=0

log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*"; }
fail() { log "ERROR: $*"; status=1; }
port_open() { lsof -nP -iTCP:"$1" -sTCP:LISTEN >/dev/null 2>&1; }

# Polls a command once a second until it succeeds or the timeout runs out.
wait_for() {
    local timeout=$1
    shift
    local i
    for ((i = 0; i < timeout; i++)); do
        "$@" && return 0
        sleep 1
    done
    return 1
}

public_site_up() {
    [ "$(curl -s -o /dev/null -w '%{http_code}' --max-time 10 "https://$DOMAIN/")" = "200" ]
}

log "startup begin"

# server.py proxies /api/trip and /api/stop to this one
if port_open 5016; then
    log "fast-static-gtfs already running"
else
    log "starting fast-static-gtfs"
    (cd "$REPO/fast-static-gtfs" && nohup ./webserver >>"$LOG_DIR/gtfs-fast-server.log" 2>&1 &)
fi

if [ -f "$GUNICORN_PID" ] && ps -p "$(cat "$GUNICORN_PID")" -o command= | grep -q gunicorn; then
    log "stopping gunicorn"
    kill "$(cat "$GUNICORN_PID")"
    wait_for 30 bash -c '! lsof -nP -iTCP:5015 -sTCP:LISTEN >/dev/null 2>&1' || fail "gunicorn did not stop"
fi
log "starting gunicorn"
# Must start from webserver/ because server.py uses relative paths
(cd "$REPO/webserver" && ../.venv/bin/gunicorn -w 2 -b 127.0.0.1:5015 server:app \
    --daemon --pid "$GUNICORN_PID" \
    --access-logfile /tmp/gtfs-gunicorn-access.log \
    --error-logfile /tmp/gtfs-gunicorn-error.log) || fail "gunicorn failed to start"
wait_for 30 curl -s -o /dev/null --max-time 2 http://127.0.0.1:5015/ || fail "gunicorn is not responding"

if ! nginx -t 2>&1; then
    fail "nginx config is invalid"
elif pgrep -f 'nginx: master' >/dev/null; then
    log "reloading nginx"
    nginx -s reload || fail "nginx reload failed"
else
    log "starting nginx"
    nginx || fail "nginx failed to start"
fi

if pgrep -f "cloudflared tunnel run" >/dev/null; then
    log "cloudflared already running"
else
    log "starting cloudflared"
    nohup cloudflared tunnel run "$TUNNEL_ID" >>"$LOG_DIR/gtfs-cloudflared.log" 2>&1 &
fi

CERT="$LE_DIR/live/$DOMAIN/cert.pem"
log "cert expiry: $(openssl x509 -enddate -noout -in "$CERT")"
if openssl x509 -checkend "$RENEW_WITHIN_SECONDS" -noout -in "$CERT" >/dev/null; then
    log "cert has more than 2 weeks left, not renewing"
# Let's Encrypt validates through the tunnel, so it has to be serving first
elif ! wait_for 300 public_site_up; then
    fail "https://$DOMAIN is unreachable, skipping cert renewal"
else
    log "cert expires within 2 weeks, reissuing"
    if certbot renew --force-renewal --non-interactive --cert-name "$DOMAIN" \
        --config-dir "$LE_DIR" --work-dir "$LE_DIR/work" --logs-dir "$LE_DIR/logs"; then
        nginx -s reload || fail "nginx reload after renewal failed"
        log "new cert expiry: $(openssl x509 -enddate -noout -in "$CERT")"
    else
        fail "certbot renewal failed, see $LE_DIR/logs/letsencrypt.log"
    fi
fi

log "startup end (status $status)"
exit "$status"
