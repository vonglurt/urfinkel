#!/bin/sh
# UR FINKEL - the COMPILED edition.  Build and boot it in VICE xplus4.
#
# This is the one that is still being developed.  The BASIC edition it was
# ported from is frozen next door and has its own launcher:
#     ../urroyal-basic/start-basic.sh
#
# Prepend Homebrew locations so this works from shells without brew on PATH.
PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
export PATH
cd "$(dirname "$0")" && exec make run
