#!/bin/bash
#
#  Copyright 2008 Search Solution Corporation
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

# Build and package script for CUBRID
# Requirements
# - Bash shell
# - Build tool - cmake, gcc
# - Utils - GNU tar, git, ...

# scrtip directory
script_dir=$(dirname $(readlink -f $0))

# variables for options
build_target="x86_64"
build_mode="release"
source_dir=`pwd`
default_java_dir="/usr/lib/jvm/java"
java_dir=""
configure_options=""
# default build_dir = "$source_dir/build_${build_target}_${build_mode}"
build_dir=""
prefix_dir=""
output_dir=""
build_args="all"
default_packages="all"
packages=""
print_version_only=0

# variables
product_name="CUBRID"
product_name_lower=$(echo $product_name | tr '[:upper:]' '[:lower:]')
typeset -i major_version
typeset -i minor_version
typeset -i patch_version
version=""
last_checking_msg=""
output_packages=""
without_cmserver=""
without_jdbc="false"

function print_check ()
{
  [ -n "$print_version_only" ] && return
  echo ""
  last_checking_msg="$@"
  echo "  $last_checking_msg..."
}

function print_result ()
{
  [ -n "$print_version_only" ] && return
  [ -n "$last_checking_msg" ] && echo -n "  "
  echo "  [$@] $last_checking_msg."
  last_checking_msg=""
}

function print_info ()
{
  [ -n "$print_version_only" ] && return
  [ -n "$last_checking_msg" ] && echo -n "  "
  echo "  [INFO] $@"
}

function print_error ()
{
  echo ""
  echo "  [ERROR] $@"
}

function print_fatal ()
{
  echo ""
  echo "  [FATAL] $@"
  echo ""
  echo "[`date +'%F %T'`] QUITTING..."
  echo ""
  exit 1
}


function build_initialize ()
{
  # check for source dir
  print_check "Checking for root source path [$source_dir]"
  if [ -d $source_dir -a -d $source_dir/src ]
  then
    print_result "OK"
  else
    print_fatal "Root path for source is not valid"
  fi

  # check Git
  print_check "Checking for Git"
  which_git=$(which git)
  [ $? -eq 0 ] && print_info "$which_git" || print_fatal "Git not found"
  print_result "OK"

  # check version
  print_check "Checking VERSION"
  if [ -f $source_dir/VERSION ]; then
    version_file=VERSION
  elif [ -f $source_dir/VERSION-DIST ]; then
    version_file=VERSION-DIST
  fi
  version=$(cat $source_dir/$version_file)
  major_version=$(echo $version | cut -d . -f 1)
  minor_version=$(echo $version | cut -d . -f 2)
  patch_version=$(echo $version | cut -d . -f 3)
  extra_version=$(echo $version | cut -d . -f 4)
  major_start_date='2019-12-12'
  if [ "x$extra_version" != "x" ]; then
    serial_number=$(echo $extra_version | cut -d - -f 1)
  elif [ -d $source_dir/.git ]; then
    serial_number=$(cd $source_dir && git rev-list --after $major_start_date --count HEAD | awk '{ printf "%04d", $1 }' 2> /dev/null)
    [ $? -ne 0 ] && serial_number=$(cd $source_dir && git log --after $major_start_date --oneline | wc -l)
    hash_tag=$(cd $source_dir && git rev-parse --short=7 HEAD)
    extra_version="$serial_number-$hash_tag"
  else
    extra_version=0000-unknown
    serial_number=0000
  fi

  print_info "version: $version ($major_version.$minor_version.$patch_version.$extra_version)"
  version="$major_version.$minor_version.$patch_version.$extra_version"

  if [ $print_version_only -eq 1 ]; then
    echo $version
    exit 0
  fi

  # old style version string (digital only version string) for legacy codes
  build_number="$major_version.$minor_version.$patch_version.$serial_number"
  print_result "OK"
}


function build_clean ()
{
  print_check "Cleaning packaging directory"
  if [ -d $prefix_dir ]; then
    if [ "$prefix_dir" = "/" ]; then
      print_fatal "Do not set root dir as install directory"
    fi
    
    print_info "All files in $prefix_dir is removing"
    rm -rf $prefix_dir/*
  fi
  print_result "OK"

  print_check "Cleaning build directory"
  if [ -d $build_dir ]; then
    if [ "$build_dir" = "/" ]; then
      print_fatal "Do not set root dir as build directory"
    fi

    if [ $build_dir -ef $source_dir ]; then
      [ -f "$build_dir/Makefile" ] && cmake --build $build_dir --target clean
    else
      print_info "All files in $build_dir is removing"
      rm -rf $build_dir/*
    fi
  fi
  print_result "OK"
}


function build_configure ()
{
  # configure with target and options
  print_check "Preparing build directory"
  if [ ! -d $build_dir ]; then
    mkdir -p $build_dir
  fi
  print_result "OK"

  print_check "Checking manager server directory"
  if [ ! -d "$source_dir/cubridmanager" -o ! -d "$source_dir/cubridmanager/server" ]; then
    without_cmserver="true"
    print_error "Manager server source path is not exist. It will not be built"
  fi

  print_check "Checking JDBC directory"
  if [ ! -d "$source_dir/cubrid-jdbc" -o ! -d "$source_dir/cubrid-jdbc/src" ]; then
    without_jdbc="true"
    print_error "JDBC source path is not exist. It will not be built"
  fi

  print_check "Setting environment variables"
  if [ "x$java_dir" != "x" ]; then
    export JAVA_HOME="$java_dir"
  elif [ "x$JAVA_HOME" = "x" -a "x$JAVA_HOME" = "x" ]; then
    export JAVA_HOME="$default_java_dir"
  fi
  export PATH="$JAVA_HOME/bin:$PATH"
  print_result "OK"

  print_check "Prepare configure options"
  # set up prefix
  configure_prefix="-DCMAKE_INSTALL_PREFIX=$prefix_dir"

  # set up target
  case "$build_target" in
    i386)
      configure_options="-DENABLE_32BIT=ON $configure_options" ;;
    x86_64);;
    *)
      print_fatal "Build target [$build_target] is not valid target" ;;
  esac

  # set up build mode
  case "$build_mode" in
    release)
      configure_options="$configure_options -DCMAKE_BUILD_TYPE=RelWithDebInfo" ;;
    debug)
      configure_options="$configure_options -DCMAKE_BUILD_TYPE=Debug" ;;
    coverage)
      configure_options="$configure_options -DCMAKE_BUILD_TYPE=Coverage" ;;
    profile)
      configure_options="$configure_options -DCMAKE_BUILD_TYPE=Profile" ;;
    *)
      print_fatal "Build mode [$build_mode] is not valid build mode" ;;
  esac
  print_result "OK"

  print_check "Configuring [with $configure_options]"
  if [ "$(readlink -f $build_dir/..)" = "$source_dir" ]; then
    configure_dir=".."
  else
    configure_dir="$source_dir"
  fi
  cmake -E chdir $build_dir cmake $configure_prefix $configure_options $source_dir
  [ $? -eq 0 ] && print_result "OK" || print_fatal "Configuring failed"
}


function build_compile ()
{
  # make
  print_check "Building"
  # Add '-j' into MAKEFLAGS environment variable to specify the number of compile jobs to run simultaneously
  if [ -n "$MAKEFLAGS" -a -z "${MAKEFLAGS##*-j*}" ]; then
    # Append '-l<num of cpu>' option into MAKEFLAGS if the '-j' option exists
    NPROC=$(grep -c '^processor' /proc/cpuinfo)
    export MAKEFLAGS="$MAKEFLAGS -l$NPROC"
  fi
  cmake --build $build_dir
  [ $? -eq 0 ] && print_result "OK" || print_fatal "Building failed"
}


function build_install ()
{
  # make install
  print_check "Installing"
  cmake --build $build_dir --target install
  [ $? -eq 0 ] && print_result "OK" || print_fatal "Installation failed"
}


function build_package ()
{
  print_check "Preparing package directory"

  if [ ! -d "$build_dir" ]; then
    print_fatal "Build directory not found. please build first"
  fi

  print_check "Checking manager server directory"
  if [ ! -d "$source_dir/cubridmanager" -o ! -d "$source_dir/cubridmanager/server" ]; then
    without_cmserver="true"
    print_error "Manager server source path is not exist. It will not be packaged"
  fi

  print_check "Checking JDBC directory"
  if [ ! -d "$source_dir/cubrid-jdbc" -o ! -d "$source_dir/cubrid-jdbc/src" ]; then
    without_jdbc="true"
    print_error "JDBC source path is not exist. It will not be packaged"
  fi

  if [ ! -d $output_dir ]; then
    mkdir -p $output_dir
  fi
  
  print_result "OK"

  for package in $packages; do
    print_check "Packing package for $package"
    case $package in
      src|zip_src)
	src_package_name="$product_name_lower-$version"
	if [ "$package" = "src" ]; then
	  package_name="$src_package_name.tar.gz"
	  archive_cmd="tar czf $build_dir/$package_name -T -"
	else
	  package_name="$src_package_name.zip"
	  archive_cmd="zip -q $build_dir/$package_name -@"
	fi
	# add VERSION-DIST instead of VERSION file for full version string
	(cd $source_dir && echo "$version" > VERSION-DIST && ln -sfT . cubrid-$version &&
	  (git ls-files -o VERSION-DIST ; git ls-files &&
	    (cd $source_dir/cubridmanager && git ls-files) | sed -e "s|^|cubridmanager/|" && 
	    ([ "$without_jdbc" = "true" ] || (cd $source_dir/cubrid-jdbc  && git ls-files -o output/VERSION-DIST; git ls-files) | sed -e "/^VERSION$/d" | sed -e "s|^|cubrid-jdbc/|")) | 
            sed -e "/^VERSION$/d" -e "/^cubrid-jdbc$/d" -e "s|^|cubrid-$version/|" | $archive_cmd &&
	    rm cubrid-$version VERSION-DIST)
	if [ $? -eq 0 ]; then
	  output_packages="$output_packages $package_name"
	  [ $build_dir -ef $output_dir ] || mv -f $build_dir/$package_name $output_dir
	else
	  false
	fi
      ;;
      tarball|shell|cci|rpm)
	if [ ! -d "$prefix_dir" ]; then
	  print_fatal "Prefix directory not found"
	fi

	if [ "$package" = "cci" ]; then
          package_basename="$product_name-CCI-$version-Linux.$build_target"
        else
          package_basename="$product_name-$version-Linux.$build_target"
        fi
	if [ ! "$build_mode" = "release" ]; then
	  package_basename="$package_basename-$build_mode"
	fi
	if [ "$package" = "tarball" ]; then
	  package_name="$package_basename.tar.gz"
	  (cd $build_dir && cpack -G TGZ -B $output_dir)
	elif [ "$package" = "shell" ]; then
	  package_name="$package_basename.sh"
	  (cd $build_dir && cpack -G STGZ -B $output_dir)
	elif [ "$package" = "cci" ]; then
	  package_name="$package_basename.tar.gz"
	  (cd $build_dir && cpack -G TGZ -D CPACK_COMPONENTS_ALL="CCI" -B $output_dir)
	elif [ "$package" = "rpm" ]; then
	  package_name="$package_basename.rpm"
	  (cd $build_dir && cpack -G RPM -B $output_dir)
	fi
	if [ $? -eq 0 ]; then
	  output_packages="$output_packages $package_name"
	  # clean temp directory for pack
	  rm -rf $output_dir/_CPack_Packages
	else
	  false
	fi
      ;;
      jdbc)
        if [ "$without_jdbc" = "false" ]; then
          jar_files=$(ls $source_dir/cubrid-jdbc/JDBC-*.jar)
          jdbc_version=$(cat $source_dir/cubrid-jdbc/output/VERSION-DIST)
          package_name="JDBC-$jdbc_version-$product_name_lower"
          cp $source_dir/cubrid-jdbc/JDBC-*.jar $output_dir
          [ $? -eq 0 ] && output_packages="$output_packages $jar_files"
        fi
      ;;
    esac
    [ $? -eq 0 ] && print_result "OK [$package_name]" || print_fatal "Packaging for $package failed"
  done
}


function build_post ()
{
  # post job
  echo "[`date +'%F %T'`] Completed"
  echo ""
  echo "*** Summary ***"
  echo "  Target [$build_args]"
  echo "  Version [$version]"
  echo "  Build mode [$build_target/$build_mode]"
  if [ -n "$configure_options" ]; then
    echo "    Configured with [$configure_options]"
  fi
  if [ -n "$output_packages" ]; then
    echo "  Generated packages in [$output_dir]"
    for pkg in $(echo $output_packages|tr " " "\n"|sort -u|tr "\n" " "); do
      echo "    -" $(cd $output_dir && md5sum $pkg)
    done
  fi
  echo ""
}


function show_usage ()
{
  echo "Usage: $0 [OPTIONS] [TARGET]"
  echo " OPTIONS"
  echo "  -t arg  Set target machine (32(i386) or 64(x86_64)); [default: 64]"
  echo "  -m      Set build mode(release, debug or coverage); [default: release]"
  echo "  -i      Increase build number; [default: no]"
  echo "  -a      Run autogen.sh before build; [default: yes]"
  echo "  -c opts Set configure options; [default: NONE]"
  echo "  -s path Set source path; [default: current directory]"
  echo "  -b path Set build path; [default: <source path>/build_<mode>_<target>]"
  echo "  -p path Set prefix path; [default: <build_path>/_install/$product_name]"
  echo "  -o path Set package output path; [default: <build_path>]"
  if [ "x$JAVA_HOME" = "x" ]; then
    echo "  -j path Set JAVA_HOME path; [default: /usr/java/default]"
  else
    echo "  -j path Set JAVA_HOME path; [default: $JAVA_HOME]"
  fi
  echo "  -z arg  Package to generate (src,zip_src,shell,tarball,cci,jdbc,rpm,owfs);"
  echo "          [default: all]"
  echo "  -? | -h Show this help message and exit"
  echo ""
  echo " TARGET"
  echo "  all     Build and create packages (default)"
  echo "  build   Build only"
  echo "  dist    Create packages only"
  echo ""
  echo " EXAMPLES"
  echo "  $0                         # Build and pack all packages (64/release)"
  echo "  $0 -t 32 build             # 32bit release build only"
  echo "  $0 -t 64 -m debug dist     # Create 64bit debug mode packages"
  echo ""
}


function get_options ()
{
  while getopts ":t:m:is:b:p:o:aj:c:z:vh" opt; do
    case $opt in
      t ) build_target="$OPTARG" ;;
      m ) build_mode="$OPTARG" ;;
      s ) source_dir="$OPTARG" ;;
      b ) build_dir="$OPTARG" ;;
      p ) prefix_dir="$OPTARG" ;;
      o ) output_dir="$OPTARG" ;;
      j ) java_dir="$OPTARG" ;;
      c )
	for optval in "$OPTARG"
	do
	  configure_options="$configure_options $optval"
	done
      ;;
      z )
	for optval in "$OPTARG"
	do
	  packages="$packages $optval"
	done
      ;;
      v ) print_version_only=1 ;;
      h|\?|* ) show_usage; exit 1;;
    esac
  done
  shift $(($OPTIND - 1))

  case $build_target in
    i386|x86|32|32bit) build_target="i386";;
    x86_64|x64|64|64bit) build_target="x86_64";;
    *) show_usage; print_fatal "Target [$build_target] is not valid target" ;;
  esac

  case $build_mode in
    release|debug|coverage);;
    *) show_usage; print_fatal "Mode [$build_mode] is not valid mode" ;;
  esac

  if [ "x$build_dir" = "x" ]; then
    build_dir="$source_dir/build_${build_target}_${build_mode}"
  fi
  # convert paths to absolute path
  [ ! -d "$build_dir" ] && mkdir -p $build_dir
  build_dir=$(readlink -f $build_dir)

  if [ "x$prefix_dir" = "x" ]; then
    prefix_dir="$build_dir/_install/$product_name"
  else
    [ ! -d "$prefix_dir" ] && mkdir -p $prefix_dir
    prefix_dir=$(readlink -f $prefix_dir)
  fi

  source_dir=$(readlink -f $source_dir)
  if [ ! -d "$source_dir" ]; then
    print_fatal "Source path [$source_dir] is not exist"
  fi

  [ "x$packages" = "x" ] && packages=$default_packages
  for i in $packages; do
    if [ "$i" = "all" -o "$i" = "ALL" ]; then
      packages="all"
      break
    fi
  done
  if [ "$packages" = "all" -o "$packages" = "ALL" ]; then
    case $build_mode in
      release)
	packages="src zip_src tarball shell cci jdbc rpm"
	;;
      *)
	packages="tarball shell jdbc cci"
	;;
    esac
  fi

  if [ "x$output_dir" = "x" ]; then
    output_dir="$build_dir"
  fi
  [ ! -d "$output_dir" ] && mkdir -p $output_dir
  output_dir=$(readlink -f $output_dir)

  if [ $# -gt 0 ]; then
    build_args="$@"
    echo "[`date +'%F %T'`] Build target [$build_args]"
  fi
}


function build_dist ()
{
  if [ "$build_mode" = "coverage" ]; then
    print_error "Packages with coverage mode is not supported. Skip"
    return 0
  fi

  build_package
}


function build_build ()
{
  build_configure && build_compile && build_install
}



# main
{
  get_options "$@"
} &&
{
  build_initialize
} &&
{
  declare -f target

  if [ "$build_args" = "all" -o "$build_args" = "ALL" ]; then
    case $build_mode in
      release|debug)
	build_args="clean build dist"
	;;
      *)
	build_args="clean build"
	;;
    esac
  fi

  for i in $build_args; do
    target=$i
    echo ""
    echo "[`date +'%F %T'`] Entering target [$target]"
    build_$target
    if [ $? -ne 0 ]; then
      echo ""
      print_fatal "*** [`date +'%F %T'`] Failed target [$target]"
    fi
    echo ""
    echo "[`date +'%F %T'`] Leaving target [$target]"
    echo ""
  done
} &&
{
  build_post
}
