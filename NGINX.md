# nginx + gunicorn setup

The Flask app (`webserver/server.py`) is served by **gunicorn** (WSGI server) on
`127.0.0.1:5015`. **nginx** sits in front of it as a reverse proxy — it terminates
TLS, serves ACME challenges for cert renewal, and forwards everything else to
gunicorn.

```
browser → nginx (:80/:443) → gunicorn (127.0.0.1:5015) → Flask app (server.py)
```

`server.py` uses relative paths (e.g. `./tools/searchstop`), so gunicorn **must**
be started with `webserver/` as the working directory. Its `/api/trip` and
`/api/stop` routes are proxied to `fast-static-gtfs/webserver` on `127.0.0.1:5016`,
which has to be running too (see [fast-static-gtfs/README.md](fast-static-gtfs/README.md)).

---

## macOS (Homebrew)

### Install

```zsh
brew install nginx
cd /Users/jettmu/Coding/gtfs-parser/webserver
source ../.venv/bin/activate   # or create one: python3 -m venv ../.venv
pip install gunicorn flask
```

Homebrew nginx paths (Apple Silicon — `/usr/local/...` on Intel Macs):

| Purpose | Path |
|---|---|
| Main config | `/opt/homebrew/etc/nginx/nginx.conf` |
| Site configs (auto-included) | `/opt/homebrew/etc/nginx/servers/*` |
| Docroot | `/opt/homebrew/var/www` |
| Binary | `/opt/homebrew/opt/nginx/bin/nginx` |

`nginx.conf` defaults to port `8080` so it can run without `sudo`, and already
has `include servers/*;` at the bottom of the `http` block — any `.conf`
dropped in `servers/` is picked up automatically (after a reload).

### Site configs

`/opt/homebrew/etc/nginx/servers/gtfs-parser.conf` — plain HTTP, local testing:

```nginx
server {
    listen 8090;
    server_name localhost;

    location / {
        proxy_pass         http://127.0.0.1:5015;
        proxy_set_header   Host              $host;
        proxy_set_header   X-Real-IP         $remote_addr;
        proxy_set_header   X-Forwarded-For   $proxy_add_x_forwarded_for;
        proxy_set_header   X-Forwarded-Proto $scheme;
    }
}
```

`/opt/homebrew/etc/nginx/servers/gtfs-jettm-dev.conf` — public domain with TLS:

```nginx
server {
    listen 80;
    server_name gtfs.jettm.dev;

    location /.well-known/acme-challenge/ {
        root /opt/homebrew/var/www/gtfs-jettm-dev;
    }

    location / {
        return 301 https://$host$request_uri;
    }
}

server {
    listen 443 ssl;
    server_name gtfs.jettm.dev;

    ssl_certificate     /Users/jettmu/letsencrypt/live/gtfs.jettm.dev/fullchain.pem;
    ssl_certificate_key /Users/jettmu/letsencrypt/live/gtfs.jettm.dev/privkey.pem;

    location / {
        proxy_pass         http://127.0.0.1:5015;
        proxy_set_header   Host              $host;
        proxy_set_header   X-Real-IP         $remote_addr;
        proxy_set_header   X-Forwarded-For   $proxy_add_x_forwarded_for;
        proxy_set_header   X-Forwarded-Proto $scheme;
    }
}
```

### Gunicorn

```zsh
# start (must run from webserver/)
cd /Users/jettmu/Coding/gtfs-parser/webserver
../.venv/bin/gunicorn -w 2 -b 127.0.0.1:5015 server:app \
  --daemon --pid /tmp/gtfs-gunicorn.pid \
  --access-logfile /tmp/gtfs-gunicorn-access.log \
  --error-logfile /tmp/gtfs-gunicorn-error.log

# stop
kill "$(cat /tmp/gtfs-gunicorn.pid)"

# status
ps -p "$(cat /tmp/gtfs-gunicorn.pid)"
```

### nginx

```zsh
nginx              # start
nginx -s reload    # reload after editing servers/*.conf
nginx -s stop      # stop
nginx -t            # validate config
```

Neither process is registered with `launchd`/`brew services` here — both need
restarting manually after reboot/logout. To have nginx persist across
reboots/logins instead, use `brew services start nginx` in place of the bare
`nginx` command (mutually exclusive with the manual approach above — pick one).

### Cert renewal (Let's Encrypt via manual certbot)

Cert expires 2026-11-13.

```zsh
certbot renew --config-dir ~/letsencrypt --work-dir ~/letsencrypt/work --logs-dir ~/letsencrypt/logs
nginx -s reload
```

---

## Ubuntu / Linux

### Install

```bash
sudo apt update
sudo apt install nginx python3-venv
cd /path/to/gtfs-parser/webserver
python3 -m venv ../.venv
source ../.venv/bin/activate
pip install gunicorn flask
```

Standard Debian/Ubuntu nginx paths:

| Purpose | Path |
|---|---|
| Main config | `/etc/nginx/nginx.conf` |
| Available sites | `/etc/nginx/sites-available/` |
| Enabled sites (symlinks) | `/etc/nginx/sites-enabled/` |
| Docroot | `/var/www/html` |

`/etc/nginx/nginx.conf` already includes `/etc/nginx/sites-enabled/*` — a site
is only live once it has a symlink there.

### Site config

`/etc/nginx/sites-available/gtfs-parser` (local/plain-HTTP testing, equivalent
to the Mac `:8090` block):

```nginx
server {
    listen 8090;
    server_name localhost;

    location / {
        proxy_pass         http://127.0.0.1:5015;
        proxy_set_header   Host              $host;
        proxy_set_header   X-Real-IP         $remote_addr;
        proxy_set_header   X-Forwarded-For   $proxy_add_x_forwarded_for;
        proxy_set_header   X-Forwarded-Proto $scheme;
    }
}
```

`/etc/nginx/sites-available/gtfs-jettm-dev` (public domain with TLS — paths
below get filled in automatically by `certbot --nginx`, see below):

```nginx
server {
    listen 80;
    server_name gtfs.jettm.dev;

    location / {
        proxy_pass         http://127.0.0.1:5015;
        proxy_set_header   Host              $host;
        proxy_set_header   X-Real-IP         $remote_addr;
        proxy_set_header   X-Forwarded-For   $proxy_add_x_forwarded_for;
        proxy_set_header   X-Forwarded-Proto $scheme;
    }
}
```

Enable a site and reload:

```bash
sudo ln -s /etc/nginx/sites-available/gtfs-parser /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl reload nginx
```

If the default site is still enabled and conflicting on port 80, remove it:

```bash
sudo rm /etc/nginx/sites-enabled/default
```

### Gunicorn — quick/manual (mirrors the Mac daemon approach)

```bash
cd /path/to/gtfs-parser/webserver
../.venv/bin/gunicorn -w 2 -b 127.0.0.1:5015 server:app \
  --daemon --pid /tmp/gtfs-gunicorn.pid \
  --access-logfile /tmp/gtfs-gunicorn-access.log \
  --error-logfile /tmp/gtfs-gunicorn-error.log

kill "$(cat /tmp/gtfs-gunicorn.pid)"          # stop
ps -p "$(cat /tmp/gtfs-gunicorn.pid)"         # status
```

### Gunicorn — systemd service (recommended for Linux; survives reboot)

`/etc/systemd/system/gtfs-gunicorn.service`:

```ini
[Unit]
Description=Gunicorn instance for gtfs-parser webserver
After=network.target

[Service]
User=www-data
Group=www-data
WorkingDirectory=/path/to/gtfs-parser/webserver
Environment="PATH=/path/to/gtfs-parser/.venv/bin"
ExecStart=/path/to/gtfs-parser/.venv/bin/gunicorn -w 2 -b 127.0.0.1:5015 server:app \
  --access-logfile /var/log/gtfs-gunicorn/access.log \
  --error-logfile /var/log/gtfs-gunicorn/error.log
Restart=always

[Install]
WantedBy=multi-user.target
```

Replace `/path/to/gtfs-parser` with the real deploy path, and `User=`/`Group=`
with whichever account owns the compiled `tools/` binaries (they need to be
executable by that user). Create the log directory first: `sudo mkdir -p
/var/log/gtfs-gunicorn && sudo chown www-data:www-data /var/log/gtfs-gunicorn`.

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now gtfs-gunicorn   # start now + on boot
sudo systemctl restart gtfs-gunicorn        # restart after code changes
sudo systemctl status gtfs-gunicorn         # status
sudo journalctl -u gtfs-gunicorn -f         # tail logs
```

### nginx

```bash
sudo systemctl start nginx
sudo systemctl reload nginx    # after editing sites-available/*
sudo systemctl stop nginx
sudo nginx -t                  # validate config
sudo systemctl enable nginx    # start on boot
```

### Cert renewal (Let's Encrypt via official certbot + nginx plugin)

Unlike the Mac setup (manual certbot + hand-written TLS config), on Linux the
official `certbot` nginx plugin edits the site config for you and sets up a
systemd timer for auto-renewal.

```bash
sudo apt install certbot python3-certbot-nginx
sudo certbot --nginx -d gtfs.jettm.dev
```

This rewrites `/etc/nginx/sites-available/gtfs-jettm-dev` in place to add the
`listen 443 ssl;` block, `ssl_certificate`/`ssl_certificate_key` pointing at
`/etc/letsencrypt/live/gtfs.jettm.dev/`, and an HTTP→HTTPS redirect — same
shape as the hand-written Mac config above.

```bash
sudo certbot renew --dry-run    # test renewal
sudo systemctl status certbot.timer   # auto-renewal timer (installed by the certbot package)
```

---

## Troubleshooting

**"Address already in use" on port 5015** — gunicorn is likely already
running (e.g. as a daemon from a previous session). Find and confirm before
killing anything:

```bash
lsof -nP -iTCP:5015 -sTCP:LISTEN
```

If it's the gunicorn master listed in `/tmp/gtfs-gunicorn.pid`, either use the
already-running instance or stop it with `kill "$(cat /tmp/gtfs-gunicorn.pid)"`
before starting a new one (e.g. via `python3 server.py` for the Flask dev
server).
