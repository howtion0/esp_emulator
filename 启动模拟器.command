#!/bin/bash
set -e
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
cd "$(dirname "$0")/desktop"
if [ ! -x node_modules/.bin/electron ]; then
  npm ci
fi
exec npm start
