#!/usr/bin/env bash
set -e

echo ""
echo "  Installing await..."
echo "  36KB CLI — polls commands until they succeed"
echo ""

curl -fsSL https://i.jpillora.com/slavaGanzin/await! | bash

echo ""
echo "  await installed! Run 'await --help' to get started."
echo "  Tip: run 'await --autocompletions' to set up shell completions."
echo ""
