#!/bin/sh
#
#  Copyright 2026 CUBRID Corporation
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
#
#

set -efu

git_executable=$1
source_dir=$2
cache_file=${3-}

# Hooks can pass repository and object paths that override git -C.
git_local_vars=$("$git_executable" rev-parse --local-env-vars)
for var in $git_local_vars; do
    unset "$var"
done

revision_info=$("$git_executable" -C "$source_dir" rev-parse --is-shallow-repository --verify 'HEAD^{commit}')
{
    read -r shallow
    read -r revision
} <<EOF
$revision_info
EOF

# Deepening a shallow clone can change the count without moving HEAD.
if [ "$shallow" = false ] && [ -n "$cache_file" ] && [ -f "$cache_file" ] &&
   read -r cached_revision count short_revision cached_shallow < "$cache_file" &&
   [ "$cached_revision" = "$revision" ] && [ "$cached_shallow" = false ]; then
    printf '%s %s %s %s\n' "$revision" "$count" "$short_revision" "$shallow"
    exit 0
fi

count=$("$git_executable" -C "$source_dir" rev-list --after 2019-12-12 --count "$revision")
short_revision=$("$git_executable" -C "$source_dir" rev-parse --short=7 "$revision")
printf '%s %04d %s %s\n' "$revision" "$count" "$short_revision" "$shallow"
