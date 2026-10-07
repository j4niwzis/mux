#!/bin/sh
# tools/prune_caches.sh PREFIX KEEP
#
# Drops the Actions caches under PREFIX that are older than KEEP, the one
# this run has just saved. Each run saves a build of ~2 GB under a key of
# its own and the repository has 10 GB: left alone, GitHub evicted the
# oldest -- the toolchains among them, built again with new file times,
# which made every object of the build restored out of date.
#
# Nothing goes where KEEP was not saved: the older ones are all there is.
# Caches saved after KEEP, by a run that started later, are left as well.
set -eu
prefix=$1
keep=$2
repo=${GITHUB_REPOSITORY:?}
list=$(gh cache list --repo "$repo" --key "$prefix" --limit 100 --sort created_at --order asc --json key --jq '.[].key')
if ! printf '%s\n' "$list" | grep -qxF "$keep"; then
  echo "$keep is not saved: nothing dropped"
  exit 0
fi
printf '%s\n' "$list" | while IFS= read -r key; do
  [ "$key" = "$keep" ] && break
  echo "dropping $key"
  gh cache delete "$key" --repo "$repo" || echo "could not drop $key"
done
