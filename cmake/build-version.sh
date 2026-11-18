#!/bin/bash
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

set -euo pipefail

fail()
{
  printf '%s\n' "$*" >&2
  exit 1
}

clear_git_environment()
{
  local variables name
  variables=$(git rev-parse --local-env-vars) || return
  while IFS= read -r name; do
    unset "$name"
  done <<< "$variables"
}

resolve_version()
{
  local source=$1 revision=${2-} version_dir=${3:-$1}
  local version count abbreviation

  if [[ -f "$version_dir/VERSION" ]]; then
    version=$(< "$version_dir/VERSION")
  elif [[ -f "$version_dir/VERSION-DIST" ]]; then
    version=$(< "$version_dir/VERSION-DIST")
  else
    fail "Missing VERSION or VERSION-DIST in $version_dir."
  fi
  version=${version%$'\r'}

  if [[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    if [[ -e "$source/.git" ]]; then
      clear_git_environment || return
      if [[ -z $revision ]]; then
        revision=$(git -C "$source" rev-parse --verify 'HEAD^{commit}') || return
      fi
      count=$(git -C "$source" rev-list --after=2019-12-12T00:00:00Z --count "$revision") || return
      abbreviation=$(git -C "$source" rev-parse --short=7 "$revision") || return
      printf -v version '%s.%04d-%s' "$version" "$count" "$abbreviation"
    else
      version+=.0
    fi
  fi

  [[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9_]+)?$ ]] ||
    fail "Invalid version in $version_dir: $version"
  printf '%s\n' "$version"
}

write_generated()
(
  local path=$1 content=$2 temporary
  temporary=$(mktemp "$path.XXXXXX")
  trap 'rm -f -- "${temporary:?}"' EXIT
  printf '%s\n' "$content" > "$temporary"
  chmod a+r -- "$temporary"
  mv -fT -- "$temporary" "$path"
)

prepare_version()
{
  local source=$1 build=$2 version_dir=$3 version
  local version_file="$version_dir/VERSION-DIST" header="$build/build_version.h"
  local numeric extra header_content

  version=$(resolve_version "$source")
  if [[ ! -f $version_file || $(< "$version_file") != "$version" ]]; then
    rm -f -- "${version_dir:?}/package-ready"
    write_generated "$version_file" "$version"
  fi

  numeric=${version%%-*}
  extra=${numeric##*.}
  header_content=$(sed -e "s/@CUBRID_VERSION@/$version/g" \
    -e "s/@BUILD_NUMBER@/$numeric/g" -e "s/@CUBRID_EXTRA_VERSION@/$extra/g" \
    "$source/cmake/build_version.h.cmake")
  if [[ ! -f $header || $(< "$header") != "$header_content" ]]; then
    write_generated "$header" "$header_content"
  fi
}

check_package_version()
{
  local version_dir=$1 expected=$2 version
  if [[ ! -f "$version_dir/package-ready" || ! -f "$version_dir/VERSION-DIST" ]]; then
    fail "Build CUBRID before invoking CPack, or use the package target."
  fi

  version=$(< "$version_dir/VERSION-DIST")
  [[ $version == "$expected" ]] ||
    fail "CUBRID package version $version does not match $expected. Build and package again."
  printf '%s\n' "$version"
}

export_submodule()
{
  local source=$1 destination=$2 revision=$3 submodule=$4
  local type commit entry version_file base_version

  [[ -e "$source/$submodule/.git" ]] || return 0
  entry=$(git -C "$source" ls-tree "$revision" -- "$submodule")
  read -r _ type commit _ <<< "$entry"
  [[ $type == commit ]] || return 0

  mkdir -p -- "$destination/$submodule"
  git -C "$source/$submodule" archive "$commit" | tar -xf - -C "$destination/$submodule"

  case $submodule in
    cubrid-cci) version_file=CCI-VERSION-DIST; base_version=BUILD_NUMBER ;;
    cubrid-jdbc) version_file=output/VERSION-DIST; base_version=VERSION ;;
    *) return 0 ;;
  esac
  if [[ -f "$source/$submodule/$version_file" &&
        $(git -C "$source/$submodule" rev-parse HEAD) == "$commit" ]]; then
    mkdir -p -- "$destination/$submodule/$(dirname "$version_file")"
    cp -- "$source/$submodule/$version_file" "$destination/$submodule/$version_file"
    rm -f -- "${destination:?}/${submodule:?}/${base_version:?}"
  fi
}

export_source()
{
  local source destination expected=$3 build revision version submodule
  local -a exclude=()
  source=$(realpath -- "$1")
  destination=$(realpath -m -- "$2")
  build=$(realpath -m -- "$4")
  [[ $source != "$destination" && $source != "$destination/"* ]] ||
    fail "Invalid source package staging directory: $destination"
  mkdir -p -- "$destination"

  if [[ -e "$source/.git" ]]; then
    clear_git_environment
    revision=$(git -C "$source" rev-parse --verify 'HEAD^{commit}')
    git -C "$source" archive "$revision" | tar -xf - -C "$destination"
    version=$(resolve_version "$source" "$revision" "$destination")
    [[ $version == "$expected" ]] || fail "Source version changed during packaging."
    for submodule in cubridmanager cubrid-jdbc cubrid-cci; do
      export_submodule "$source" "$destination" "$revision" "$submodule"
    done
  else
    version=$(resolve_version "$source")
    [[ $version == "$expected" ]] || fail "Source version changed during packaging."
    [[ $build != "$source/"* ]] || exclude+=(--exclude="./${build#"$source"/}")
    [[ $destination != "$source/"* ]] || exclude+=(--exclude="./${destination#"$source"/}")
    tar -cf - -C "$source" --exclude='.git' --exclude='./build_*' --exclude='./_CPack_Packages' "${exclude[@]}" . |
      tar -xf - -C "$destination"
  fi

  rm -f -- "${destination:?}/VERSION"
  write_generated "$destination/VERSION-DIST" "$version"
}

case ${1-} in
  resolve) resolve_version "$2" ;;
  prepare) prepare_version "$2" "$3" "$4" ;;
  check) check_package_version "$2" "$3" ;;
  export) export_source "$2" "$3" "$4" "$5" ;;
  *) fail "Expected resolve, prepare, check or export." ;;
esac
