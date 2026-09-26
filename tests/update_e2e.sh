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

log=$(env -u AWAIT_RELEASES_URL -u AWAIT_UPDATE_TARGET AWAIT_UPDATE_FORCE=1 "$dir/await" --update 2>&1) \
  || fail "await --update failed: $log"
echo "$log"

# check against exactly what the updater fetched (not a separate lookup of
# "latest", which a release published meanwhile could change)
url=$(printf '%s\n' "$log" | sed -n 's/^await: downloading //p')
case $url in "$repo"/download/*/await-*.tar.gz) ;; *) fail "--update didn't say what it downloaded";; esac
tag=${url#"$repo"/download/}; tag=${tag%%/*}
latest=${tag#v}
echo "update e2e: installed release $tag over this build ($built)"

# the installed file is exactly the one in the release archive for this machine
mkdir "$dir/release"
curl -fsSL --retry 3 -o "$dir/release.tar.gz" "$url" && tar -xzf "$dir/release.tar.gz" -C "$dir/release" \
  || fail "couldn't fetch $url to compare"
cmp -s "$dir/release/await" "$dir/await" || fail "installed await isn't the one in $url"
cmp -s "$bin" "$dir/await.old" || fail "backup isn't the build we started from"
[ -x "$dir/await" ] || fail "installed await isn't executable"
rm -rf "$dir/release" "$dir/release.tar.gz"

got=$("$dir/await" --version)
[ "$got" = "$latest" ] || fail "installed await reports '$got', expected $latest"
old=$("$dir/await.old" --version)
[ "$old" = "$built" ] || fail "backup reports '$old', expected $built"
out=$("$dir/await" 'echo ok' --stdout --silent)
[ "$out" = ok ] || fail "installed await doesn't run commands (got '$out')"
leftovers=$(find "$dir" -name '.await-update*')
[ -z "$leftovers" ] || fail "left behind: $leftovers"

# without the override, the release is not newer than itself (unless a newer
# one was published meanwhile, which a plain update then rightly installs)
msg=$(env -u AWAIT_UPDATE_FORCE "$dir/await" --update 2>&1) || fail "second --update failed: $msg"
case $msg in *"already up to date ($latest)"*|*"updated $latest -> "*) ;; *) fail "second --update said: $msg";; esac

echo "update e2e: ok ($built -> $latest from $url, backup kept)"
