#!/usr/bin/env python3
"""
sync_tokenserver.py
Generates tokenserver.py from server.py automatically.
Usage: python3 sync_tokenserver.py
"""

import re

INPUT  = 'server.py'
OUTPUT = 'tokenserver.py'

TOKEN_IMPORTS = """from functools import wraps
from t_confidental_info import token
"""

TOKEN_SETUP = """
SECRET_TOKENS = token

def require_token(f):
    @wraps(f)
    def decorated(*args, **kwargs):
        token = request.headers.get("X-Auth-Token") or request.args.get("token")
        if token not in SECRET_TOKENS:
            abort(401)
        return f(*args, **kwargs)
    return decorated
"""

# Routes that should NOT get @require_token
EXCLUDED_ROUTES = {
    '',
    '/favicon.ico',  # already unprotected in tokenserver.py
}

def main():
    with open(INPUT, 'r') as f:
        src = f.read()

    # 1. Update imports: ensure request/abort are imported, add functools + token import
    src, n = re.subn(
        r"^from flask import Flask, jsonify, send_from_directory.*?# type: ignore\n",
        "from flask import Flask, jsonify, send_from_directory, request, abort # type: ignore\n" + TOKEN_IMPORTS,
        src,
        count=1,
        flags=re.MULTILINE
    )
    if n == 0:
        raise RuntimeError(f"Could not find flask import line in {INPUT}")

    # 2. Inject SECRET_TOKEN + require_token after app = Flask(__name__)
    src = src.replace(
        'app = Flask(__name__)\n',
        'app = Flask(__name__)\n' + TOKEN_SETUP
    )

    # 3. Remove the /crash test route (not present in tokenserver.py) before
    #    decorators are injected, so the pattern still matches.
    src = re.sub(
        r'@app\.route\("/crash"\)\ndef home\(\):\n    raise Exception\("test error"\) \n\n',
        '',
        src
    )

    # 4. Add @require_token under every @app.route(...) EXCEPT excluded ones
    def inject_decorator(match):
        route_decorator = match.group(0)
        route_path_match = re.search(r"@app\.route\('([^']+)'", route_decorator)
        if route_path_match and route_path_match.group(1) in EXCLUDED_ROUTES:
            return route_decorator  # skip excluded routes
        # Only add if not already there
        if '@require_token' not in route_decorator:
            return route_decorator + '@require_token\n'
        return route_decorator

    src = re.sub(r"@app\.route\([^\)]+\)\n", inject_decorator, src)

    # 5. Serve every page from tksweb/ (token-aware copies made by
    #    tokenindex_converter.py) instead of web/
    src, n = re.subn(r"send_from_directory\('web',", "send_from_directory('tksweb',", src)
    if n == 0:
        raise RuntimeError("No send_from_directory('web', ...) page routes found in " + INPUT)

    # 6. Force the token server to always run on 0.0.0.0:5015 with debug off,
    #    regardless of what server.py is currently set to.
    src = re.sub(
        r"app\.run\([^)]*\)",
        "app.run(debug=False, port=5015, host='0.0.0.0')",
        src
    )

    with open(OUTPUT, 'w') as f:
        f.write(src)

    print(f"✓ {OUTPUT} generated from {INPUT}")

if __name__ == '__main__':
    main()