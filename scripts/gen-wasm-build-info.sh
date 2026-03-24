#!/bin/sh

set -eu

out_file=$1
repo_url="https://github.com/sobomax/digger"
revision="unknown"
short_revision="unknown"
dirty=false

if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    revision=$(git rev-parse HEAD 2>/dev/null || printf '%s' "$revision")
    short_revision=$(git rev-parse --short=12 HEAD 2>/dev/null || printf '%s' "$short_revision")
    if ! git diff --no-ext-diff --quiet --exit-code HEAD -- 2>/dev/null; then
        dirty=true
    fi
fi

revision_url=$repo_url
if [ "$revision" != "unknown" ]; then
    revision_url="$repo_url/commit/$revision"
fi

cat > "$out_file" <<EOF
(function() {
  window.getDiggerBuildInfo = function() {
    return {
      revision: "$revision",
      shortRevision: "$short_revision",
      dirty: $dirty,
      revisionUrl: "$revision_url"
    };
  };
}());
EOF
