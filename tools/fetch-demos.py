#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Fetch a small corpus of Total Annihilation demos from tademos.xyz.

The demos are the conformance corpus described in docs/TA-DEMOS.md: recordings
of real games whose build timings, economy curves and weapon events are ground
truth that a hand-written fixture cannot invent. This script gets enough of them
to prove the parser; the episode extraction that uses them at scale is a later
piece of work.

Be civil with that archive. It is volunteer-run and it is doing us a favour, so
this script is deliberately slow and deliberately cached:

  * one request at a time, never concurrent
  * at least DELAY seconds between every request, index pages included
  * a demo already on disk is never fetched again
  * --limit defaults to 10 and you have to ask for more
  * it stops on the first error rather than retrying in a loop

If you have been sent an archive of demos, use that instead and do not run this
at all -- tad_probe --dir takes a directory whatever filled it.

Demos are NEVER written into the repository. docs/TA-DEMOS.md is explicit that
the episodes get checked in and the demos do not, so the default output
directory is outside the tree and you have to pass --dir to change it.

Most of the archive is modded, and that matters in a specific way rather than a
general one. A mod that is pure data records TotalA.exe's arithmetic just as
faithfully as vanilla does, so its demos are perfectly good evidence; a mod that
patches the engine is a different engine and its demos are not. Of the two in
the corpus so far, ProTA 4.8 (mod 25) ships a byte-identical TotalA.exe and
TA: Escalation 10.2 (mod 29) ships a patched one. So --mod and --exclude-mod are
about picking a known engine, not about purity.

Usage:
    uv run tools/fetch-demos.py --dir /tmp/tad --limit 10
    uv run tools/fetch-demos.py --dir /tmp/tad --exclude-mod 29 --pages 5
"""

import argparse
import json
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

BASE = "https://tademos.xyz"
USER_AGENT = "RWE-tad-probe/1.0 (Robot War Engine; conformance corpus; contact via github.com/MHeasell/rwe)"

# Seconds between requests. Not a tunable: the point is to be a good citizen.
DELAY = 2.0

DEMO_LINK = re.compile(r'href="(/demos/(\d+))"')
BLOB_LINK = re.compile(r'href="(/rails/active_storage/blobs/redirect/[^"]+)"')
MOD_LINK = re.compile(r'href="(/ta_mods/(\d+))"')
MAP_LINK = re.compile(r'href="(/ta_maps/(\d+))"')
TITLE = re.compile(r"<h1[^>]*>([^<]*)")

_last_request = 0.0


def get(url: str) -> bytes:
    """One request, never sooner than DELAY seconds after the last."""
    global _last_request

    wait = DELAY - (time.monotonic() - _last_request)
    if wait > 0:
        time.sleep(wait)

    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            body = response.read()
    except urllib.error.HTTPError as e:
        raise SystemExit(f"{url}: HTTP {e.code}. Stopping rather than retrying.")
    except urllib.error.URLError as e:
        raise SystemExit(f"{url}: {e.reason}. Stopping rather than retrying.")
    finally:
        _last_request = time.monotonic()

    return body


def list_demo_ids(pages: int) -> list[str]:
    ids: list[str] = []
    seen: set[str] = set()

    for page in range(1, pages + 1):
        url = f"{BASE}/demos" if page == 1 else f"{BASE}/demos?page={page}"
        print(f"index: {url}", file=sys.stderr)
        html = get(url).decode("utf-8", "replace")

        for _, demo_id in DEMO_LINK.findall(html):
            if demo_id not in seen:
                seen.add(demo_id)
                ids.append(demo_id)

    return ids


def fetch_demo(demo_id: str, out_dir: Path, only_mod: str | None, not_mods: set[str]) -> bool:
    """Fetch one demo and its provenance. Returns False if it was already here."""
    sidecar = out_dir / f"{demo_id}.json"
    if sidecar.exists():
        existing = json.loads(sidecar.read_text())
        if (out_dir / existing["file"]).exists():
            print(f"{demo_id}: cached", file=sys.stderr)
            return False

    page_url = f"{BASE}/demos/{demo_id}"
    html = get(page_url).decode("utf-8", "replace")

    title_match = TITLE.search(html)
    mod_match = MOD_LINK.search(html)
    map_match = MAP_LINK.search(html)

    # The mod filter is applied here, from the demo page, rather than by walking
    # /ta_mods/<id> as an index. That would be one request instead of one per
    # demo, but this page is the one whose shape is known -- MOD_LINK is already
    # proven against it by every sidecar on disk -- and guessing at a page layout
    # to save requests on a volunteer-run site is the wrong trade. The demo file
    # itself is still only fetched for a demo that passes.
    mod_id = mod_match.group(2) if mod_match else None
    if only_mod is not None and mod_id != only_mod:
        print(f"{demo_id}: mod {mod_id or 'none'}, wanted {only_mod}, skipping", file=sys.stderr)
        return False
    if mod_id is not None and mod_id in not_mods:
        print(f"{demo_id}: mod {mod_id} excluded, skipping", file=sys.stderr)
        return False

    blob = BLOB_LINK.search(html)
    if not blob:
        print(f"{demo_id}: no download link, skipping", file=sys.stderr)
        return False

    blob_url = urllib.parse.urljoin(BASE, blob.group(1))
    name = urllib.parse.unquote(blob_url.rsplit("/", 1)[-1])

    # Do not trust Path.suffix here: these filenames are built from player names
    # and one of them will have a dot in it ("EINS.pro"), which turns into a
    # bogus extension and a file tad_probe then declines to look at.
    suffix = ".tad" if name.lower().endswith(".tad") else ".ted"

    # Name by id so the sidecar and the demo stay together and nothing collides.
    demo_path = out_dir / f"{demo_id}{suffix}"
    print(f"{demo_id}: {name}", file=sys.stderr)
    demo_path.write_bytes(get(blob_url))

    sidecar.write_text(
        json.dumps(
            {
                "id": demo_id,
                "file": demo_path.name,
                "page": page_url,
                "title": title_match.group(1).strip() if title_match else None,
                # The archive tags each demo with the mod it was played under,
                # which is the cheap way to keep a corpus vanilla. The demo's own
                # extra sector 7 says the same thing; having both lets one check
                # the other.
                "mod": urllib.parse.urljoin(BASE, mod_match.group(1)) if mod_match else None,
                "map": urllib.parse.urljoin(BASE, map_match.group(1)) if map_match else None,
            },
            indent=2,
        )
        + "\n"
    )

    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dir", required=True, type=Path, help="where to write demos; must not be inside the repo")
    parser.add_argument("--limit", type=int, default=10, help="how many demos to fetch (default 10)")
    parser.add_argument("--pages", type=int, default=1, help="how many index pages to walk (default 1)")
    parser.add_argument("--mod", help="fetch only demos the archive tags with this mod id (25 is ProTA 4.8, 29 is TA:Esc 10.2)")
    parser.add_argument(
        "--exclude-mod",
        action="append",
        default=[],
        metavar="ID",
        help="skip demos tagged with this mod id; may be repeated",
    )
    args = parser.parse_args()

    if args.mod is not None and args.mod in args.exclude_mod:
        raise SystemExit(f"--mod {args.mod} and --exclude-mod {args.mod} cannot both be asked for")

    repo = Path(__file__).resolve().parent.parent
    out_dir = args.dir.resolve()
    if repo in out_dir.parents or repo == out_dir:
        raise SystemExit(f"refusing to write demos into the repository at {repo}; see docs/TA-DEMOS.md")

    if args.limit > 25:
        raise SystemExit(
            f"--limit {args.limit} is more than this needs. A dozen demos proves the parser; "
            "if you want a large corpus, ask someone for an archive rather than taking it a "
            "file at a time from a volunteer-run site."
        )

    out_dir.mkdir(parents=True, exist_ok=True)

    ids = list_demo_ids(args.pages)
    print(f"{len(ids)} demos listed, fetching up to {args.limit}", file=sys.stderr)

    fetched = 0
    for demo_id in ids:
        if fetched >= args.limit:
            break
        if fetch_demo(demo_id, out_dir, args.mod, set(args.exclude_mod)):
            fetched += 1

    print(f"\n{fetched} fetched into {out_dir}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
