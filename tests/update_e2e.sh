#!/bin/sh
# End-to-end check of `await --update` against the real GitHub releases: a
# copy of this build installs the latest published release over itself, the
# way a user's update would (download for this OS/CPU, SHA256SUMS, test run,
# backup, swap). A build is never older than what's released, so
# AWAIT_UPDATE_FORCE lets it "update" to a release that isn't newer.
#
#   tests/update_e2e.sh [path/to/await]    (default: ./await)
set -eu

bin=${1:-./await}
repo=https://github.com/slavaGanzin/await/releases
fail() { echo "update e2e: $*" >&2; exit 1; }

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
cp "$bin" "$dir/await"
built=$("$dir/await" --version)

latest=$(curl -fsSI --retry 3 "$repo/latest" | tr -d '\r' | sed -n 's/^[Ll]ocation: *//p' | tail -n1)
latest=${latest##*/tag/}
[ -n "$latest" ] || fail "couldn't read the latest release from $repo/latest"
echo "update e2e: installing release $latest over this build ($built)"

log=$(env -u AWAIT_RELEASES_URL -u AWAIT_UPDATE_TARGET AWAIT_UPDATE_FORCE=1 "$dir/await" --update 2>&1) \
  || fail "await --update failed: $log"
echo "$log"

# the installed file is exactly the one in the release archive for this machine
target=$(printf '%s\n' "$log" | sed -n 's/.*downloading await [^ ]* (\(.*\))$/\1/p')
[ -n "$target" ] || fail "--update didn't say which build it downloaded"
mkdir "$dir/release"
curl -fsSL --retry 3 "$repo/download/$latest/await-$latest-$target.tar.gz" | tar -xzf - -C "$dir/release"
cmp -s "$dir/release/await" "$dir/await" || fail "installed await isn't the $target build from release $latest"
cmp -s "$bin" "$dir/await.old" || fail "backup isn't the build we started from"
[ -x "$dir/await" ] || fail "installed await isn't executable"
rm -rf "$dir/release"

got=$("$dir/await" --version)
[ "$got" = "$latest" ] || fail "installed await reports '$got', expected $latest"
old=$("$dir/await.old" --version)
[ "$old" = "$built" ] || fail "backup reports '$old', expected $built"
out=$("$dir/await" 'echo ok' --stdout --silent)
[ "$out" = ok ] || fail "installed await doesn't run commands (got '$out')"
leftovers=$(find "$dir" -name '.await-update*')
[ -z "$leftovers" ] || fail "left behind: $leftovers"

# without the override, the release is not newer than itself
msg=$(env -u AWAIT_UPDATE_FORCE "$dir/await" --update 2>&1) || fail "second --update failed: $msg"
case $msg in *"already up to date"*) ;; *) fail "second --update said: $msg";; esac

echo "update e2e: ok ($built -> $latest, backup kept)"
