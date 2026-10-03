# Converting the normal server to the token server

`tokenserver.py` and `tksweb/` are **generated** — don't hand-edit them. From `webserver/`:

1. Create `t_confidental_info.py` (gitignored, never commit it):
```py
token = "enter_token_here"   # or a list of tokens
```
2. Regenerate both variants whenever `server.py` or `web/*.html` change:
```zsh
python3 tokenserver_converter.py   # server.py -> tokenserver.py
python3 tokenindex_converter.py    # web/*.html -> tksweb/*.html
```
3. Run it (binds `0.0.0.0:5015`, `debug=False`):
```zsh
python3 tokenserver.py
```
Access with `http://<host>:5015/?token=<secret>` (or an `X-Auth-Token` header for API calls).

What the converters do:
- `tokenserver_converter.py` injects a `require_token` decorator on every route, strips the `/crash` test route, serves pages from `tksweb/` instead of `web/`, and forces `debug=False, host='0.0.0.0'`.
- `tokenindex_converter.py` injects a token-reading `apiFetch()` wrapper, swaps bare `fetch(` for `apiFetch(` inside `<script>` blocks, and appends `?token=` to internal links.
