#!/bin/bash
#
#
#  Copyright 2016 CUBRID Corporation
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

# The full CUBRID version of a Linux build (major.minor.patch.count-hash), resolved in one place.
#
#   build-version.sh resolve <source_dir>
#     Print the full version of the source tree: build.sh -v and the source packages use it.
#
#   build-version.sh prepare <source_dir> <binary_dir> <version_dir>
#     Run by the cubrid_build_version target at every build. It resolves the version of the commit being built and
#     rewrites <version_dir>/VERSION-DIST and <binary_dir>/build_version.h only when the version changed, so a
#     commit on the same branch updates the version without re-running CMake and an unchanged version recompiles
#     nothing. CPack takes the package version from this VERSION-DIST, the version of the binaries it packages.

set -euo pipefail

# the commit count of the version counts the commits after this date
major_start_date=2019-12-12

fail ()
{
  printf '%s\n' "$*" >&2
  exit 1
}

resolve_version ()
{
  local source=$1
  local version count hash variable

  if [[ -f "$source/VERSION" ]]; then
    version=$(< "$source/VERSION")
  elif [[ -f "$source/VERSION-DIST" ]]; then
    version=$(< "$source/VERSION-DIST")
  else
    fail "Could not find a VERSION file in $source"
  fi
  # CMake accepts surrounding spaces and a CR line ending in VERSION
  version=${version//[[:space:]]/}

  if [[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    if [[ -e "$source/.git" ]] && command -v git > /dev/null; then
      # GIT_DIR and the like are set when this runs under a Git hook; they would point git at another repository.
      # A git without --local-env-vars prints the option back, so unset only GIT_ names, and the main ones anyway.
      for variable in GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR \
        $(git rev-parse --local-env-vars 2> /dev/null); do
        if [[ $variable == GIT_* ]]; then
          unset "$variable"
        fi
      done
      # cd instead of git -C, and git log for git older than 1.7.2 without rev-list --count, as the CMake code did
      cd "$source" || fail "Could not enter $source"
      count=$(git rev-list --count --after "$major_start_date" HEAD 2> /dev/null) ||
        count=$(git log --after "$major_start_date" --oneline | wc -l) ||
        fail "Could not get count information from Git"
      hash=$(git rev-parse --short=7 HEAD) || fail "Could not get hash information from Git"
      printf -v version '%s.%04d-%s' "$version" "$count" "$hash"
    else
      version+=.0
    fi
  fi

  [[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9_]+)?$ ]] || fail "Invalid version in $source: $version"
  printf '%s\n' "$version"
}

# replace <path> with <content> only when it differs, so that the build tool sees no change otherwise
update_file ()
{
  local path=$1 content=$2 temporary

  if [[ -f $path && $(< "$path") == "$content" ]]; then
    return 0
  fi
  temporary=$(mktemp "$path.XXXXXX")
  printf '%s\n' "$content" > "$temporary"
  chmod a+r "$temporary"
  mv -f "$temporary" "$path"
}

prepare_version ()
{
  local source=$1 binary=$2 version_dir=$3
  local version build_number extra_version header

  version=$(resolve_version "$source")
  build_number=${version%%-*}
  extra_version=${build_number##*.}
  header=$(sed -e "s/@CUBRID_VERSION@/$version/g" -e "s/@BUILD_NUMBER@/$build_number/g" \
    -e "s/@CUBRID_EXTRA_VERSION@/$extra_version/g" "$source/cmake/build_version.h.cmake")

  update_file "$version_dir/VERSION-DIST" "$version"
  update_file "$binary/build_version.h" "$header"
}

case ${1-} in
  resolve)
    [[ $# -eq 2 ]] || fail "usage: $0 resolve <source_dir>"
    resolve_version "$2"
    ;;
  prepare)
    [[ $# -eq 4 ]] || fail "usage: $0 prepare <source_dir> <binary_dir> <version_dir>"
    prepare_version "$2" "$3" "$4"
    ;;
  *)
    fail "usage: $0 resolve|prepare ..."
    ;;
esac
