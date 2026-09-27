#!/usr/bin/env python3
"""Upload a release that `make release` built: `make publish`.

    publish-release.py [<version>]

Takes build/release/<version>/ - the newest one if no version is given -
and makes the GitHub release for that tag: the title from the first line of
NOTES.md, the rest as the body, and every file in the folder but NOTES.md as
an asset.  A draft or an existing release for the tag is updated rather than
duplicated, and its assets replaced.

IT UPLOADS ONLY WHAT release.sh BUILT AND HASHED.  Nothing is read from
build/ itself, which the hooks rebuild; that is how a release once went out
with a zip that did not match its own notes.  Afterwards every asset is
downloaded back from GitHub and checked against the folder's SHA256SUMS.

The token comes from $GH_TOKEN, or else ~/.config/urfinkel-gh-token.  It
needs Contents: read and write on this repository, and is never printed.
"""
import hashlib, json, os, sys, urllib.error, urllib.request

REPO = "vonglurt/urfinkel"
BASE = "https://api.github.com/repos/%s/releases" % REPO
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RELDIR = os.path.join(ROOT, "build", "release")


def token():
    t = os.environ.get("GH_TOKEN")
    if not t:
        path = os.path.expanduser("~/.config/urfinkel-gh-token")
        if not os.path.exists(path):
            sys.exit("publish: no token - set GH_TOKEN or write ~/.config/urfinkel-gh-token")
        t = open(path).read()
    return t.strip()


TOKEN = token()


def api(method, url, body=None, data=None, ctype="application/json", raw=False):
    if body is not None:
        data = json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, method=method, headers={
        "Authorization": "Bearer " + TOKEN,
        "Accept": "application/octet-stream" if raw else "application/vnd.github+json",
        "Content-Type": ctype,
    })
    try:
        with urllib.request.urlopen(req) as r:
            b = r.read()
            return b if raw else (json.loads(b) if b else None)
    except urllib.error.HTTPError as e:
        sys.exit("publish: %s %s -> %d: %s" % (method, url, e.code, e.read().decode()[:300]))


def main():
    if len(sys.argv) > 1:
        v = sys.argv[1]
    else:
        have = sorted(os.listdir(RELDIR)) if os.path.isdir(RELDIR) else []
        if not have:
            sys.exit("publish: nothing in build/release - run `make release` first")
        v = have[-1]
    d = os.path.join(RELDIR, v)
    notes = open(os.path.join(d, "NOTES.md")).read()
    first, _, rest = notes.partition("\n")
    name, body = (first[2:].strip(), rest.lstrip("\n")) if first.startswith("# ") else ("UR FINKEL " + v, notes)
    assets = sorted(f for f in os.listdir(d) if f != "NOTES.md")
    want = {}
    for line in open(os.path.join(d, "SHA256SUMS")):
        h, f = line.split()
        want[f] = h

    fields = {"tag_name": v, "name": name, "body": body,
              "draft": False, "prerelease": False, "make_latest": "true"}
    existing = [r for r in api("GET", BASE + "?per_page=100") if r["tag_name"] == v]
    if len(existing) > 1:
        sys.exit("publish: %d releases use tag %s - sort those out by hand" % (len(existing), v))
    rel = api("PATCH", "%s/%d" % (BASE, existing[0]["id"]), fields) if existing else api("POST", BASE, fields)
    print("publish: %s release %s" % ("updated" if existing else "created", v))

    old = {a["name"]: a["id"] for a in rel["assets"]}
    for f in assets:
        if f in old:
            api("DELETE", "%s/assets/%d" % (BASE, old[f]))
        blob = open(os.path.join(d, f), "rb").read()
        api("POST", rel["upload_url"].split("{")[0] + "?name=" + f, data=blob,
            ctype="application/octet-stream")
        print("publish: uploaded %s (%d bytes)" % (f, len(blob)))

    ok = True
    for a in api("GET", "%s/%d" % (BASE, rel["id"]))["assets"]:
        if a["name"] not in want:
            continue
        got = hashlib.sha256(api("GET", a["url"], raw=True)).hexdigest()
        match = got == want[a["name"]]
        ok &= match
        print("publish: %s %s" % ("OK " if match else "BAD", a["name"]))
    print(rel["html_url"])
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
