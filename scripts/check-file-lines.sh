#!/usr/bin/env bash
set -euo pipefail

limit=5000
status=0

while IFS= read -r file; do
    lines=$(wc -l < "$file")
    if (( lines > limit )); then
        printf 'line-limit violation: %s has %d lines (limit %d)\n' "$file" "$lines" "$limit"
        status=1
    fi
done < <(
    find .github include src docs scripts tests examples cmake \
        -type f \
        \( -name '*.c' -o -name '*.h' -o -name '*.md' -o -name '*.yml' -o -name '*.yaml' -o -name '*.sh' -o -name '*.cneg' -o -name '*.cn' -o -name '*.S' -o -name '*.cmake' \) \
        | sort
)

if (( $(wc -l < CMakeLists.txt) > limit )); then
    printf 'line-limit violation: %s has %d lines (limit %d)\n' "CMakeLists.txt" "$(wc -l < CMakeLists.txt)" "$limit"
    status=1
fi

exit "$status"
