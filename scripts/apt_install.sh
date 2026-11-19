#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
#
# CI's apt install, with every network step bounded. A throttled Ubuntu mirror
# (77 kB/s from the runners' regional mirror on 2026-11-18) never fails a
# transfer, so apt's own retries never start; the job then sits in the install
# until its timeout cancels it. This script gives each network step a time
# budget and, when a step runs out of it, moves the preferred mirror to the end
# of the runner's mirror list and tries again. Files that were only partly
# downloaded are resumed, not fetched again.
#
#   scripts/apt_install.sh [--archives DIR] [--] INSTALL_ARGS...
#
# INSTALL_ARGS go to `apt-get install` as they are: the packages and flags
# such as --no-install-recommends. --archives DIR keeps the .debs in DIR (an
# actions/cache path) instead of /var/cache/apt/archives, and leaves DIR owned
# by the caller, without apt's lock or partial downloads, and with only the
# .debs the current indexes still list, ready for the cache to save it.
#
# Environment: APT_INSTALL_ATTEMPTS (default 3) attempts, each with a budget of
# APT_INSTALL_BUDGET seconds (default 180) for the index refresh and the same
# again for the download, 15 s apart: at most about 18.5 minutes when every
# mirror is slow, so the script reports it inside a 20-minute job. A mirror
# that delivers the uncached Arm toolchain (about 600 MB) within the budget
# runs at 3.3 MB/s or more; at a healthy one it takes about a minute.
#
# The mirror list is the GitHub-hosted runners' /etc/apt/apt-mirrors.txt
# (apt-transport-mirror: one URI per line, tab-separated metadata, the lowest
# priority:N tried first). Without that file the attempts retry the same
# sources.
set -euo pipefail

archives=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --archives)
            archives="${2:?--archives needs a directory}"
            shift 2
            ;;
        --)
            shift
            break
            ;;
        *) break ;;
    esac
done
if [[ $# -eq 0 ]]; then
    echo "usage: $0 [--archives DIR] [--] INSTALL_ARGS..." >&2
    exit 2
fi

attempts="${APT_INSTALL_ATTEMPTS:-3}"
budget="${APT_INSTALL_BUDGET:-180}"
mirrors=/etc/apt/apt-mirrors.txt

# Stalls (no byte for 30 s) and refused connections are apt's to retry; a
# lock held by the image's own apt jobs at boot is waited for, not failed on.
apt_opts=(-q -y -o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30
    -o DPkg::Lock::Timeout=120)
if [[ -n "$archives" ]]; then
    archives="$(mkdir -p "$archives" && cd "$archives" && pwd)"
    apt_opts+=(-o "Dir::Cache::Archives=$archives/")
fi
# Root, with no package asking a question.
as_root=(sudo env DEBIAN_FRONTEND=noninteractive)

# Runs one network step under the budget; on overrun, says what it got to.
bounded() {
    local what="$1"
    shift
    local start=$SECONDS status=0
    "${as_root[@]}" timeout --kill-after=30 "$budget" apt-get "${apt_opts[@]}" "$@" || status=$?
    if [[ $status -eq 0 ]]; then
        return 0
    fi
    if [[ $status -eq 124 || $status -eq 137 ]]; then
        local partial=""
        if [[ -n "$archives" && -d "$archives/partial" ]]; then
            partial=" ($(sudo du -sh "$archives/partial" | cut -f1) partly downloaded, kept for the next attempt)"
        fi
        echo "::warning::apt $what ran out of its ${budget} s budget after $((SECONDS - start)) s$partial"
    else
        echo "::warning::apt $what failed with status $status after $((SECONDS - start)) s"
    fi
    return 1
}

# Moves the preferred mirror to the end of the list, keeping every line's
# other metadata, and renumbers the priorities in the new order.
rotate_mirrors() {
    if [[ ! -f "$mirrors" ]]; then
        echo "no $mirrors: retrying the same sources"
        return 0
    fi
    local order
    order="$(awk -F'\t' '
        NF && $1 !~ /^#/ {
            p = 1000000; rest = $1
            for (i = 2; i <= NF; ++i) {
                if ($i ~ /^priority:/) { p = substr($i, 10) + 0 } else { rest = rest "\t" $i }
            }
            printf "%d\t%d\t%s\n", p, NR, rest
        }' "$mirrors" | sort -n -k1,1 -k2,2 | cut -f3-)"
    if [[ "$(wc -l <<<"$order")" -lt 2 ]]; then
        echo "one mirror in $mirrors: retrying it"
        return 0
    fi
    {
        tail -n +2 <<<"$order"
        head -n 1 <<<"$order"
    } | awk -F'\t' '{ printf "%s\tpriority:%d\n", $0, NR }' | sudo tee "$mirrors.new" >/dev/null
    sudo mv "$mirrors.new" "$mirrors"
    echo "::warning::moved $(head -n 1 <<<"$order" | cut -f1) to the end of the mirror list"
    cat "$mirrors"
}

if [[ -f "$mirrors" ]]; then
    echo "mirror list ($mirrors):"
    cat "$mirrors"
fi

# The network steps: refresh the indexes and download every .deb the install
# needs, with the budget, the attempts and the mirror rotation.
attempt=1
until bounded "update" update && bounded "download" install --download-only "$@"; do
    if [[ $attempt -ge $attempts ]]; then
        echo "::error::apt could not download the packages in $attempts attempts of ${budget} s"
        exit 1
    fi
    attempt=$((attempt + 1))
    rotate_mirrors
    # A mirror mid-sync (a hash mismatch, a missing file) clears in seconds.
    sleep 15
    echo "attempt $attempt of $attempts"
done

# The install itself reads only the downloaded .debs: nothing here waits on
# the network, and nothing a budget could interrupt runs dpkg.
"${as_root[@]}" apt-get "${apt_opts[@]}" install --no-download "$@"

if [[ -n "$archives" ]]; then
    "${as_root[@]}" apt-get "${apt_opts[@]}" autoclean
    sudo chown -R "$(id -u):$(id -g)" "$archives"
    rm -rf "${archives:?}/partial" "${archives:?}/lock"
fi
