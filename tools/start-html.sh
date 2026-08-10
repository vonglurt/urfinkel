#!/bin/sh
#
# Serve this folder and open the play page in a browser.
#
# WHY A SERVER IS NEEDED AT ALL.  index.html asks the emulator to fetch
# urfinkel.prg from beside it.  A page opened as a file:// URL is not allowed
# to make that read - every browser treats it as a cross-origin request to a
# null origin and refuses - so the emulator would boot and then sit at an
# empty Plus/4 with nothing to run.  Served over http://localhost the read is
# ordinary and it works.  That is the entire purpose of this script.
#
# IT DOES NOT INSIST ON PYTHON.  Any static file server will do, so this tries
# the ones a machine is likely to already have, in order, and uses the first
# it finds.  On a Mac that is usually python3 from the Command Line Tools, but
# a stock system with no developer tools installed has none of them - so when
# nothing is found it says exactly what to install and why, rather than
# failing with a bare "command not found".
#
# Ctrl-C stops it.

set -e

dir=$(cd "$(dirname "$0")" && pwd)

# ---------------------------------------------------------------- the port
# Ask the OS for a free port rather than guessing 8000 and colliding with
# whatever is already there.  Without python3 to ask with, probe upward from
# 8000 with nc; if there is no nc either, take 8000 and let it fail loudly.
find_port() {
    if command -v python3 >/dev/null 2>&1; then
        python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()'
        return
    fi
    if command -v nc >/dev/null 2>&1; then
        p=8000
        while [ "$p" -lt 8100 ]; do
            nc -z 127.0.0.1 "$p" >/dev/null 2>&1 || { echo "$p"; return; }
            p=$((p + 1))
        done
    fi
    echo 8000
}

# ------------------------------------------------------------- the servers
# One function each, because they disagree about how to be told a directory
# and a port, and a table of strings would not survive the quoting.
serve_python3()   { python3 -m http.server "$port" --bind 127.0.0.1 --directory "$dir"; }
serve_python2()   { cd "$dir" && python -m SimpleHTTPServer "$port"; }
serve_php()       { php -S "127.0.0.1:$port" -t "$dir"; }
serve_ruby()      { ruby -run -e httpd "$dir" -p "$port" -b 127.0.0.1; }
serve_miniserve() { miniserve --index index.html -p "$port" -i 127.0.0.1 "$dir"; }
serve_darkhttpd() { darkhttpd "$dir" --addr 127.0.0.1 --port "$port"; }
serve_busybox()   { busybox httpd -f -p "127.0.0.1:$port" -h "$dir"; }
serve_npx()       { npx --yes serve -l "$port" "$dir"; }

# npx is last on purpose: it is the only one that may reach the network to
# fetch the server before it can serve anything, which is a poor thing to do
# inside a bundle whose whole point is working offline.
pick_server() {
    for candidate in python3 python php ruby miniserve darkhttpd busybox npx; do
        command -v "$candidate" >/dev/null 2>&1 || continue
        case "$candidate" in
            python3)   server=serve_python3;   label="python3 -m http.server" ;;
            python)    python -c 'import SimpleHTTPServer' >/dev/null 2>&1 || continue
                       server=serve_python2;   label="python -m SimpleHTTPServer" ;;
            php)       server=serve_php;       label="php -S" ;;
            ruby)      server=serve_ruby;      label="ruby -run -e httpd" ;;
            miniserve) server=serve_miniserve; label="miniserve" ;;
            darkhttpd) server=serve_darkhttpd; label="darkhttpd" ;;
            busybox)   server=serve_busybox;   label="busybox httpd" ;;
            npx)       server=serve_npx;       label="npx serve" ;;
        esac
        return 0
    done
    return 1
}

if ! pick_server; then
    cat >&2 <<'NOSERVER'
start-html.sh: no static file server found on this machine.

  This script only needs something - anything - that can serve a folder over
  http://localhost.  It looked for python3, python, php, ruby, miniserve,
  darkhttpd, busybox and npx, and found none of them.

  ON macOS, the shortest way to get one is Apple's own developer tools.  It
  is a system dialog and a few hundred megabytes, no third-party anything:

      xcode-select --install

  That puts python3 on your PATH, and this script will then work.

  If you would rather have Homebrew - worth it if you also want VICE, which
  plays this game far better than a browser does:

      /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
      brew install python          # or: brew install miniserve
      brew install vice            # then: xplus4 urfinkel.prg

  ON DEBIAN OR UBUNTU:

      sudo apt install python3

  OR SKIP THE BROWSER ENTIRELY.  The browser copy is the least of the three
  ways to play this, and the only one that needs a server or a network at
  all.  urfinkel.prg and urfinkel.d64 are sitting next to this script and
  VICE runs them directly:

      xplus4 urfinkel.prg

  See RUNNING.txt for that route and for the real hardware one.
NOSERVER
    exit 1
fi

port=$(find_port)
url="http://localhost:$port/"

echo "UR FINKEL - serving $dir"
echo "           $url"
echo "           using $label"
echo "           Ctrl-C to stop."
echo

# The server's own output is kept rather than thrown away.  When one of these
# dies on startup the reason is in there - a busy port, a missing library, a
# permission - and guessing at it in the message below would be worse than
# useless, because the guess is usually "port in use" and usually wrong.
log=${TMPDIR:-/tmp}/urfinkel-serve.$$
$server >"$log" 2>&1 &
server_pid=$!
trap 'rm -f "$log"' EXIT

# Stop the server on the way out however we leave - Ctrl-C included, which is
# how anyone will actually end this.  Replaces the log-cleanup trap above and
# does that job too.
trap 'kill $server_pid 2>/dev/null; rm -f "$log"; exit 0' INT TERM EXIT

# Give it a moment to bind before pointing a browser at it, or the first
# request can arrive at a closed socket and show a connection error.
sleep 1

if ! kill -0 $server_pid 2>/dev/null; then
    echo "start-html.sh: $label exited immediately. It said:" >&2
    echo >&2
    sed 's/^/    /' "$log" >&2 2>/dev/null || cat "$log" >&2
    echo >&2
    echo "  If that is a busy port, run this again - a free one is chosen" >&2
    echo "  each time.  Otherwise the server itself is the problem, and" >&2
    echo "  RUNNING.txt lists the others this script will accept, as well" >&2
    echo "  as how to play without a browser at all." >&2
    exit 1
fi

if   command -v open     >/dev/null 2>&1; then open "$url"
elif command -v xdg-open >/dev/null 2>&1; then xdg-open "$url"
else echo "Open this in your browser: $url"
fi

wait $server_pid
