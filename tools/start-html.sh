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
# It needs python3, which macOS and every Linux ship.  Ctrl-C stops it.

set -e

dir=$(cd "$(dirname "$0")" && pwd)

if ! command -v python3 >/dev/null 2>&1; then
    echo "start-html.sh: python3 not found." >&2
    echo >&2
    echo "  It is needed only to serve this folder.  Any other static server" >&2
    echo "  will do the same job - from this directory, for example:" >&2
    echo "      npx serve ." >&2
    echo >&2
    echo "  Or skip the browser entirely and use VICE:  xplus4 urfinkel.prg" >&2
    exit 1
fi

# Ask the operating system for a port that is free rather than guessing one
# and failing on a machine that already has something on 8000.
port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')
url="http://localhost:$port/"

echo "UR FINKEL - serving $dir"
echo "           $url"
echo "           Ctrl-C to stop."
echo

python3 -m http.server "$port" --bind 127.0.0.1 --directory "$dir" >/dev/null 2>&1 &
server=$!

# Stop the server on the way out however we leave - Ctrl-C included, which is
# how anyone will actually end this.
trap 'kill $server 2>/dev/null; exit 0' INT TERM EXIT

# Give it a moment to bind before pointing a browser at it, or the first
# request can arrive to a closed socket and show a connection error.
sleep 1

if   command -v open     >/dev/null 2>&1; then open "$url"
elif command -v xdg-open >/dev/null 2>&1; then xdg-open "$url"
else echo "Open this in your browser: $url"
fi

wait $server
