#!/usr/bin/env python3
"""
sync_tokenindex.py
Generates every page in tksweb/ from its counterpart in web/ automatically.
Usage (from webserver/): python3 tokenindex_converter.py
"""

import os
import re

BASE = os.path.dirname(os.path.abspath(__file__))
INPUT_DIR  = os.path.join(BASE, 'web')
OUTPUT_DIR = os.path.join(BASE, 'tksweb')
PAGES = ['index.html', 'depboard.html', 'rteboard.html']

# Injected just before todayStr()
TOKEN_JS = """    const token = new URLSearchParams(window.location.search).get('token');

    function apiFetch(url, options = {}) {
        return fetch(url, {
        ...options,
        headers: { ...options.headers, 'X-Auth-Token': token }
        });
    }

    // Page navigation can't send headers, so carry the token on internal links.
    document.querySelectorAll('a[href^="/"]').forEach(a => {
        const u = new URL(a.getAttribute('href'), window.location.origin);
        u.searchParams.set('token', token);
        a.href = u.pathname + u.search;
    });

"""

def replace_fetch_in_scripts(html):
    """Replace bare fetch( with apiFetch( inside <script> blocks only."""
    result = []
    last = 0
    for m in re.finditer(r'(<script\b[^>]*>)(.*?)(</script>)', html, flags=re.DOTALL):
        result.append(html[last:m.start()])
        result.append(m.group(1))
        js = re.sub(r'(?<!api)fetch\(', 'apiFetch(', m.group(2))
        # Restore the real fetch( inside apiFetch's own return statement
        js = js.replace('return apiFetch(url,', 'return fetch(url,')
        result.append(js)
        result.append(m.group(3))
        last = m.end()
    result.append(html[last:])
    return ''.join(result)

def convert(name):
    with open(os.path.join(INPUT_DIR, name), 'r') as f:
        src = f.read()

    # 1. Inject token + apiFetch just before function todayStr()
    src, n = re.subn(
        r'(\n\s*function todayStr\(\))',
        lambda m: '\n' + TOKEN_JS + m.group(1),
        src,
        count=1
    )
    if n == 0:
        raise RuntimeError(f"function todayStr() not found in web/{name}")

    # 2. Replace all bare fetch( with apiFetch( inside <script> blocks
    src = replace_fetch_in_scripts(src)

    with open(os.path.join(OUTPUT_DIR, name), 'w') as f:
        f.write(src)

    print(f"✓ tksweb/{name} generated from web/{name}")

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    for name in PAGES:
        convert(name)

if __name__ == '__main__':
    main()
