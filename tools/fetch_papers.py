"""fetch_papers.py — research-paper fetcher for the literature program.

Downloads papers (arXiv or direct URL) into research/papers/, extracts nothing
(fetch only; extraction is `pdftotext` in the research pipeline), and records
status/URL in research/papers/manifest.json. Rate-limited and idempotent:
already-recorded entries are skipped unless --force. Never modifies a
fetched PDF. Usage: python3 tools/fetch_papers.py <arxiv-id|url>... [--force]
"""
import argparse
import json
import os
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
PAPERS_DIR = os.path.normpath(os.path.join(HERE, '..', 'research', 'papers'))
MANIFEST = os.path.join(PAPERS_DIR, 'manifest.json')
MIN_BYTES = 10000
SLEEP = 3


def load_manifest():
    if os.path.exists(MANIFEST):
        with open(MANIFEST) as f:
            return json.load(f)
    return {}


def save_manifest(m):
    with open(MANIFEST, 'w') as f:
        json.dump(m, f, indent=2, sort_keys=True)


def fetch(arxiv_id, m):
    if arxiv_id in m:
        return m[arxiv_id]['status']
    pdf_url = 'https://arxiv.org/pdf/%s' % arxiv_id
    out = os.path.join(PAPERS_DIR, arxiv_id + '.pdf')
    entry = {'id': arxiv_id, 'status': 'fetching', 'path': out}
    m[arxiv_id] = entry
    save_manifest(m)
    try:
        req = urllib.request.Request(pdf_url, headers={'User-Agent': 'research-agent/0.1'})
        with urllib.request.urlopen(req, timeout=60) as r:
            data = r.read()
        if len(data) < MIN_BYTES:
            raise RuntimeError('small download (%d bytes)' % len(data))
        with open(out, 'wb') as f:
            f.write(data)
        entry['status'] = 'ok'
        entry['bytes'] = len(data)
        save_manifest(m)
        return 'ok'
    except Exception as e:
        entry['status'] = 'failed: %s' % e
        save_manifest(m)
        return entry['status']


def main():
    ap = argparse.ArgumentParser(description='Download arXiv papers into research/papers/')
    ap.add_argument('ids', nargs='*', help='arXiv IDs, or use --file')
    ap.add_argument('--file', help='file with one arXiv ID per line (comments with #)')
    ap.add_argument('--sleep', type=float, default=SLEEP, help='seconds between requests (arXiv rate limit)')
    args = ap.parse_args()
    os.makedirs(PAPERS_DIR, exist_ok=True)
    ids = list(args.ids)
    if args.file:
        with open(args.file) as f:
            for line in f:
                line = line.split('#')[0].strip()
                if line:
                    ids.append(line)
    if not ids:
        ap.error('no arXiv IDs given')
    m = load_manifest()
    for i, aid in enumerate(ids):
        if i > 0:
            time.sleep(args.sleep)
        print('%-14s %s' % (aid, fetch(aid, m)))
    print('manifest: %s' % MANIFEST)


if __name__ == '__main__':
    main()