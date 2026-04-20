#!/bin/sh

set -eu

repo_url="https://github.com/sobomax/digger"
revision="unknown"
short_revision="unknown"
dirty=false
dirty_suffix=""
git_cmd=""

find_git() {
    if command -v git >/dev/null 2>&1; then
        printf '%s\n' git
        return 0
    fi
    if command -v git.exe >/dev/null 2>&1; then
        printf '%s\n' git.exe
        return 0
    fi
    if [ -x "/c/Program Files/Git/bin/git.exe" ]; then
        printf '%s\n' "/c/Program Files/Git/bin/git.exe"
        return 0
    fi
    return 1
}

git_run() {
    "$git_cmd" "$@"
}

if [ "${GITHUB_REPOSITORY:-}" != "" ]; then
    repo_url="https://github.com/$GITHUB_REPOSITORY"
fi

if git_cmd=$(find_git); then
    if git_run rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        revision=$(git_run rev-parse HEAD 2>/dev/null || printf '%s' "$revision")
        short_revision=$(git_run rev-parse --short=12 HEAD 2>/dev/null || printf '%s' "$short_revision")
        if ! git_run diff --no-ext-diff --quiet --exit-code HEAD -- 2>/dev/null; then
            dirty=true
            dirty_suffix="+dirty"
        fi
    fi
elif [ "${GITHUB_SHA:-}" != "" ]; then
    revision=$GITHUB_SHA
    short_revision=$(printf '%.12s' "$revision")
fi

revision_url=$repo_url
if [ "$revision" != "unknown" ]; then
    revision_url="$repo_url/commit/$revision"
fi

emit_js() {
    out_file=$1

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
}

emit_header() {
    out_file=$1

    cat > "$out_file" <<EOF
#ifndef DIGGER_VERSION_H
#define DIGGER_VERSION_H

#define DIGGER_BUILD_GIT_REVISION "$revision"
#define DIGGER_BUILD_GIT_SHORT_REVISION "$short_revision"
#define DIGGER_BUILD_GIT_DIRTY_SUFFIX "$dirty_suffix"
#define DIGGER_SIP_BRANDING \
  "Digger Reloaded (" DIGGER_BUILD_GIT_SHORT_REVISION DIGGER_BUILD_GIT_DIRTY_SUFFIX ")"

#endif
EOF
}

for out_file in "$@"; do
    case "$out_file" in
    *.js)
        emit_js "$out_file"
        ;;
    *.h)
        emit_header "$out_file"
        ;;
    *)
        printf '%s\n' "Unsupported output type: $out_file" >&2
        exit 1
        ;;
    esac
done
