#!/bin/bash
#
# Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
#
#   This program is free software; you can redistribute it and/or modify 
#   it under the terms of the GNU General Public License as published by 
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version. 
#
#  This program is distributed in the hope that it will be useful, 
#  but WITHOUT ANY WARRANTY; without even the implied warranty of 
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
#  GNU General Public License for more details. 
#
#  You should have received a copy of the GNU General Public License 
#  along with this program; if not, write to the Free Software 
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#

# Build and package script for CUBRID
# Requirements
# - Bash shell
# - Build tool - autotools, make, gcc
# - Utils - GNU tar, ...
# - RPM tool - rpmbuild

# scrtip directory
script_dir=$(dirname $(readlink -f $0))

# variables for options
build_target="x86_64"
build_mode="release"
increase_build_number="no"
source_dir=`pwd`
run_autogen="no"
default_java_dir="/usr/java/default"
java_dir=""
configure_options=""
# default build_dir = "$source_dir/build_${build_target}_${build_mode}"
build_dir=""
install_dir=""
prefix_dir=""
output_dir=""
build_args="all"
default_packages="all"
packages=""

# variables
product_name="CUBRID"
product_name_lower=$(echo $product_name | tr '[:upper:]' '[:lower:]')
typeset -i major
typeset -i minor
typeset -i maintanance
typeset -i serial
build_number=""
last_checking_msg=""
output_packages=""
without_cmserver=""

function print_check ()
{
  echo ""
  last_checking_msg="$@"
  echo "  $last_checking_msg..."
}

function print_result ()
{
  [ -n "$last_checking_msg" ] && echo -n "  "
  echo "  [$@] $last_checking_msg."
  last_checking_msg=""
}

function print_info ()
{
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
  if [ -d $source_dir -a -d $source_dir/src -a -f $source_dir/BUILD_NUMBER ]
  then
    print_result "OK"
  else
    print_fatal "Root path for source is not valid"
  fi

  print_check "Checking for compiler"
  which_gcc=$(which gcc)
  [ $? -eq 0 ] && print_info "$which_gcc" || print_fatal "GCC not found"
  gcc_version=$(gcc --version | grep GCC)
  [ $? -eq 0 ] && print_info "$gcc_version" || print_info "unknown GCC version"
  print_result "OK"

  # check version
  print_check "Checking BUILD NUMBER"
  build_number=$(cat $source_dir/BUILD_NUMBER)
  major=$(echo $build_number | cut -d . -f 1)
  minor=$(echo $build_number | cut -d . -f 2)
  maintanance=$(echo $build_number | cut -d . -f 3)
  serial=10#$(echo $build_number | cut -d . -f 4)
  print_info "version: $build_number ($major.$minor.$maintanance.$serial)"
  print_result "OK"
}


function build_increase_version ()
{
  # check for increase_build_number
  if [ "$increase_build_number" = "yes" ]; then
    print_check "Increasing BUILD NUMBER"
    serial_num=$(printf "%04d" $(expr $serial + 1))
    build_number="$major.$minor.$maintanance.$serial_num"
    #build_number=$(cat $source_dir/BUILD_NUMBER | awk -F'.' '{ printf("%d.%d.%d.%04d\n", $1, $2, $3, $4+1) }')

    print_info "Modifing Version to $build_number"
    # BUILD_NUMBER
    echo $build_number > $source_dir/BUILD_NUMBER

    # copy right
    current_year=`date +"%Y"`
    sed --in-place -r "s/Copyright \(C\) 2008-[0-9]{4} Search Solution/Copyright (C) 2008-${current_year} Search Solution/g" $source_dir/COPYING
    sed --in-place -r "s/Copyright \(C\) 2008-[0-9]{4} Search Solution/Copyright (C) 2008-${current_year} Search Solution/g" "$source_dir/win/install/Installshield/Setup Files/Compressed Files/Language Independent/OS Independent/license.txt"

    # win version
    win_version_h="$source_dir/win/version.h"
    echo "#define RELEASE_STRING ${major}.${minor}.${maintanance}" > ${win_version_h}
    echo "#define MAJOR_RELEASE_STRING ${major}.${minor}" >> ${win_version_h}
    echo "#define BUILD_NUMBER ${build_number}" >> ${win_version_h}
    echo "#define MAJOR_VERSION ${major}" >> ${win_version_h}
    echo "#define MINOR_VERSION ${minor}" >> ${win_version_h}
    echo "#define PATCH_VERSION ${maintanance}" >> ${win_version_h}
    echo "#define BUILD_SERIAL_NUMBER ${serial_num}" >> ${win_version_h}
    echo "#define VERSION_STRING \"${build_number}\"" >> ${win_version_h}
    echo "#define PRODUCT_STRING \"${major}.${minor}\"" >> ${win_version_h}
    echo "#define PACKAGE_STRING \"CUBRID ${major}.${minor}\"" >> ${win_version_h}
    echo "" >> ${win_version_h}

    # Install-Shield's release number
    sed --in-place -r "s|<td>[0-9]+\.[0-9]+</td>|<td>${major}.${minor}</td>|g" $source_dir/win/install/Installshield/CUBRID.ism
    sed --in-place -r "s|<td>ProductVersion</td><td>.*</td><td/>|<td>ProductVersion</td><td>${build_number}</td><td/>|g" $source_dir/win/install/Installshield/CUBRID.ism
    sed --in-place -r "s|<td>[0-9]+\.[0-9]+</td>|<td>${major}.${minor}</td>|g" $source_dir/win/install/Installshield/CUBRID_x64.ism
    sed --in-place -r "s|<td>ProductVersion</td><td>.*</td><td/>|<td>ProductVersion</td><td>${build_number}</td><td/>|g" $source_dir/win/install/Installshield/CUBRID_x64.ism

    # php's release number
    sed --in-place -r "s/[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+/${build_number}/g" $source_dir/contrib/php4/src/php_cubrid_version.h
    sed --in-place -r "s/[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+/${build_number}/g" $source_dir/contrib/php5/php_cubrid_version.h

    # RPM spec's release number
    sed --in-place -r -e "s/cubrid_version [0-9]+\.[0-9]+\.[0-9]+\.[0-9]+/cubrid_version ${build_number}/g" -e "s/build_version  [0-9]+\.[0-9]+\.[0-9]+/build_version  ${major}.${minor}.${maintanance}/g" $source_dir/contrib/rpm/cubrid.spec

    print_result "OK"
  fi
}


function build_clean ()
{
  print_check "Cleaning packaging directory"
  if [ -d $install_dir ]; then
    if [ "$install_dir" = "/" ]; then
      print_fatal "Do not set root dir as install directory"
    fi
    
    print_info "All files in $install_dir is removing"
    rm -rf $install_dir/*
  fi
  print_result "OK"

  print_check "Cleaning build directory"
  if [ -d $build_dir ]; then
    if [ "$build_dir" = "/" ]; then
      print_fatal "Do not set root dir as build directory"
    fi

    if [ $build_dir -ef $source_dir ]; then
      [ -f "$build_dir/Makefile" ] && make -C $build_dir distclean
    else
      print_info "All files in $build_dir is removing"
      rm -rf $build_dir/*
    fi
  fi
  print_result "OK"
}


function build_autogen ()
{
  # run autogen if needed
  if [ $run_autogen = "yes" -o $source_dir/BUILD_NUMBER -nt $source_dir/configure ]; then
    print_check "Running autogen.sh"
    (cd $source_dir && sh ./autogen.sh)
    [ $? -eq 0 ] && print_result "OK" || print_fatal "Result failed"
  fi
  
  # run autogen of cubrid manager if needed
  print_check "Checking manager server directory"
  if [ -d "$source_dir/cubridmanager" -a -d "$source_dir/cubridmanager/server" ]; then 
    print_result "OK"
    
    print_check "Running autogen.sh of cubrid manager" 
    (cd $source_dir/cubridmanager/server && sh ./autogen.sh) 
    [ $? -eq 0 ] && print_result "OK" || print_fatal "Result failed"
  else
    print_info "Manager server source path is not exist. It will not be built"
  fi
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
  configure_prefix="--prefix=$prefix_dir"

  # set up target
  case "$build_target" in
    i386)
      configure_options="--disable-64bit $configure_options" ;;
    x86_64);;
    *)
      print_fatal "Build target [$build_target] is not valid target" ;;
  esac

  # set up build mode
  case "$build_mode" in
    release) ;;
    debug)
      configure_options="$configure_options --enable-debug" ;;
    coverage)
      configure_options="$configure_options --enable-debug --enable-coverage" ;;
    *)
      print_fatal "Build mode [$build_mode] is not valid build mode" ;;
  esac

  if [ $build_mode = "release" ]; then
    # check conflict
    case "$configure_options" in
      *"--enable-debug"*)
	print_fatal "Conflict release mode with debug mode. check options";;
    esac
  fi
  print_result "OK"

  print_check "Configuring [with $configure_options]"
  if [ "$(readlink -f $build_dir/..)" = "$source_dir" ]; then
    configure_dir=".."
  else
    configure_dir="$source_dir"
  fi
  (cd $build_dir && $configure_dir/configure $configure_prefix $configure_options)
  [ $? -eq 0 ] && print_result "OK" || print_fatal "Configuring failed"
}


function build_compile ()
{
  # make
  print_check "Building"
  (cd $build_dir && make -j)
  [ $? -eq 0 ] && print_result "OK" || print_fatal "Building failed"
}


function build_install ()
{
  # make install
  print_check "Installing"
  (cd $build_dir && make install)
  [ $? -eq 0 ] && print_result "OK" || print_fatal "Installation failed"
}


function print_install_sh ()
{
cat << \END_OF_FILE
#!/bin/sh

PRODUCT_NAME=CUBRID

install_file="$0"

tail +20 "$install_file" > install.${PRODUCT_NAME}.tmp 2> /dev/null
if [ "$?" != "0" ]
then
  tail -n +20 "$install_file" > install.${PRODUCT_NAME}.tmp 2> /dev/null
fi

tar xf install.${PRODUCT_NAME}.tmp

./${PRODUCT_NAME}_Setup.sh "$0"

rm -f ./${PRODUCT_NAME}_Setup.sh ${PRODUCT_NAME}-product.tar.gz ${PRODUCT_NAME}-product.tar ./install.${PRODUCT_NAME}.tmp ./confcp.sh ./version.sh check_glibc_version COPYING

exit 0
END_OF_FILE
}

function print_setup_sh ()
{
cat << \END_OF_FILE
#!/bin/sh

PRODUCT_CODE=cubrid
PRODUCT_NAME=CUBRID

target_input=""
cp_old_dbtxt="no"
cwd=`pwd`
is_protego="no" 

cat COPYING | more -d
while true; do
	echo -n "Do you agree to the above license terms? (yes or no) : "
	read agree_terms

	if [ "$agree_terms" = "yes" ]; then
		break
	elif [ "$agree_terms" = "no" ]; then
		echo "If you don't agree to the license you can't install this software."
		exit 0
	fi
done

echo -n "Do you want to install this software($PRODUCT_NAME) to the default(${cwd}/${PRODUCT_NAME}) directory? (yes or no) [Default: yes] : "
read ans_install_dir

if [ "$ans_install_dir" = "no" ] || [ "$ans_install_dir" = "n" ]; then
	echo -n "Input the $PRODUCT_NAME install directory. [Default: ${cwd}/${PRODUCT_NAME}] : "
	read target_input
fi

if [ "$target_input" = "" ]; then
    target_dir=$cwd/$PRODUCT_NAME
else
    target_dir=$target_input
fi

echo "Install ${PRODUCT_NAME} to '$target_dir' ..."

XDBMS_ENV_FILE1=$HOME/.${PRODUCT_CODE}.csh
XDBMS_ENV_FILE2=$HOME/.${PRODUCT_CODE}.sh

if [ -d $target_dir ]; then
    echo "Directory '$target_dir' exist! "
    echo "If a ${PRODUCT_NAME} service is running on this directory, it may be terminated abnormally."
    echo "And if you don't have right access permission on this directory(subdirectories or files), install operation will be failed."
    echo -n "Overwrite anyway? (yes or no) [Default: no] : "
    read overwrite

    if [ "$overwrite" != "y" ] && [ "$overwrite" != "yes" ]; then
        exit 0
    fi
else
    mkdir -p $target_dir
fi

. ./version.sh
echo "Since CUBRID broker and server versions should match, please make sure that you are running the same version if you operate them in separate machines. For installation of CUBRID tools like Query Browser, Manager and Web Manager, please refer to http://www.cubrid.org/wiki_tools."
echo -n "Do you want to continue? (yes or no) [Default: yes] : "
read cont
if [ "$cont" = "n" ] || [ "$cont" = "no" ]; then
    exit 0
fi

if [ -w $XDBMS_ENV_FILE1 ]; then
    echo "Copying old .${PRODUCT_CODE}.csh to .${PRODUCT_CODE}.csh.bak ..."
    rm -f ${XDBMS_ENV_FILE1}.bak
    cp $XDBMS_ENV_FILE1 ${XDBMS_ENV_FILE1}.bak
fi

if [ -w $XDBMS_ENV_FILE2 ]; then
    echo "Copying old .${PRODUCT_CODE}.sh to .${PRODUCT_CODE}.sh.bak ..."
    rm -f ${XDBMS_ENV_FILE2}.bak
    cp $XDBMS_ENV_FILE2 ${XDBMS_ENV_FILE2}.bak
fi

if [ ! -w $target_dir/databases/databases.txt ]; then
    if [ -w $target_dir/CUBRID_DATABASES/databases.txt ]; then
        cp_old_dbtxt="yes"
    fi
fi

#gzip -d ${PRODUCT_CODE}-product.tar.gz
gzip -d ${PRODUCT_NAME}-product.tar.gz

(cd $target_dir && tar --extract --no-same-owner --file=$cwd/${PRODUCT_NAME}-product.tar > /dev/null 2>&1)
if [ $? != 0 ]; then
    (cd $target_dir && tar xfo $cwd/${PRODUCT_NAME}-product.tar)
    if [ $? != 0 ]; then
        exit 1
    fi
fi

mkdir -p $target_dir/var/log/error_log
chmod 777 $target_dir/var/log/error_log

target_dir=`readlink -f $target_dir`

sh_profile=""

if [ -w $target_dir/protego_manager ]; then
    is_protego="yes"
fi

case $SHELL in 
    */csh ) sh_profile=$HOME/.cshrc;;
    */tcsh )
        if [ ! -r "$HOME/.tcshrc" ]; then
            sh_profile=$HOME/.cshrc
        else
            sh_profile=$HOME/.tcshrc
        fi
        ;;
    */bash )
        if [ -r $HOME/.bash_profile ]; then
            sh_profile=$HOME/.bash_profile
        elif [ -r $HOME/.bashrc ]; then
            sh_profile=$HOME/.bashrc
        elif [ -r $HOME/.bash_login ]; then
            sh_profile=$HOME/.bash_login
        else
            sh_profile=$HOME/.profile
        fi
        ;;
    */zsh )
        if [ -r $HOME/.zprofile ]; then
            sh_profile=$HOME/.zprofile
        elif [ -r $HOME/.zshrc ]; then
            sh_profile=$HOME/.zshrc
        elif [ -r $HOME/.zshenv ]; then
            sh_profile=$HOME/.zshenv
        elif [ -r $HOME/.zlogin ]; then
            sh_profile=$HOME/.zlogin
        else
            sh_profile=$HOME/.profile
        fi 
        ;;
    */sh | */ksh | */ash | */bsh )
        sh_profile=$HOME/.profile
        ;;
esac

case $SHELL in
    */csh | */tcsh ) 
    echo "setenv    CUBRID                  $target_dir"             > $XDBMS_ENV_FILE1
    echo "setenv    CUBRID_DATABASES        $target_dir/databases"          >> $XDBMS_ENV_FILE1

    echo 'if (${?LD_LIBRARY_PATH}) then' >> $XDBMS_ENV_FILE1
    echo 'setenv    LD_LIBRARY_PATH         $CUBRID/lib:${LD_LIBRARY_PATH}'  >> $XDBMS_ENV_FILE1
    echo 'else'                                                                                     >> $XDBMS_ENV_FILE1
    echo 'setenv    LD_LIBRARY_PATH         $CUBRID/lib'     >> $XDBMS_ENV_FILE1
    echo 'endif'                                                                                    >> $XDBMS_ENV_FILE1
    echo 'setenv    SHLIB_PATH              $LD_LIBRARY_PATH'                                       >> $XDBMS_ENV_FILE1
    echo 'setenv    LIBPATH                 $LD_LIBRARY_PATH'                                       >> $XDBMS_ENV_FILE1
    echo 'set       path=($CUBRID/{bin,cubridmanager} $path)'      >> $XDBMS_ENV_FILE1
    ;;
esac

#
# make $XDBMS_ENV_FILE2 (.cubrid.sh) to make demodb/subway in this script
#
echo "CUBRID=$target_dir"                   > $XDBMS_ENV_FILE2
echo "CUBRID_DATABASES=$target_dir/databases"      >> $XDBMS_ENV_FILE2

echo 'ld_lib_path=`printenv LD_LIBRARY_PATH`'          >> $XDBMS_ENV_FILE2
echo 'if [ "$ld_lib_path" = "" ]'                      >> $XDBMS_ENV_FILE2
echo 'then'                                             >> $XDBMS_ENV_FILE2
echo 'LD_LIBRARY_PATH=$CUBRID/lib'               >> $XDBMS_ENV_FILE2
echo 'else'                                             >> $XDBMS_ENV_FILE2
echo 'LD_LIBRARY_PATH=$CUBRID/lib:$LD_LIBRARY_PATH' >> $XDBMS_ENV_FILE2
echo 'fi'                                               >> $XDBMS_ENV_FILE2
echo 'SHLIB_PATH=$LD_LIBRARY_PATH'                      >> $XDBMS_ENV_FILE2
echo 'LIBPATH=$LD_LIBRARY_PATH'                         >> $XDBMS_ENV_FILE2
echo 'PATH=$CUBRID/bin:$CUBRID/cubridmanager:$PATH' >> $XDBMS_ENV_FILE2
echo "export CUBRID"                                    >> $XDBMS_ENV_FILE2
echo "export CUBRID_DATABASES"                          >> $XDBMS_ENV_FILE2
echo 'export LD_LIBRARY_PATH'                           >> $XDBMS_ENV_FILE2
echo 'export SHLIB_PATH'                                >> $XDBMS_ENV_FILE2
echo 'export LIBPATH'                                   >> $XDBMS_ENV_FILE2
echo 'export PATH'                                      >> $XDBMS_ENV_FILE2

append_profile=""
if [ -n $sh_profile ]; then
    append_profile=`grep "${PRODUCT_NAME} environment" ${sh_profile}`
fi

if [ -z "${append_profile}" ]; then
    echo ''                                                                                 >> $sh_profile
    echo '#-------------------------------------------------------------------------------' >> $sh_profile
    echo '# set '${PRODUCT_NAME}' environment variables'                                               >> $sh_profile
    echo '#-------------------------------------------------------------------------------' >> $sh_profile

    case $SHELL in
        */csh | */tcsh )
            echo "source $XDBMS_ENV_FILE1"                                                 >> $sh_profile
        ;;
        * )
            echo ". $XDBMS_ENV_FILE2"                                                      >> $sh_profile
        ;;
    esac

    echo ''                                                                                 >> $sh_profile
    echo ''                                                                                 >> $sh_profile
fi

if [ $? = 0 ]; then
    echo ""
    echo "${PRODUCT_NAME} has been successfully installed."
    echo ""
else
    echo ""
    echo "Cannot install CUBRID."
    echo ""
    exit 1
fi

# Make demodb
if [ -r "$XDBMS_ENV_FILE2" ]; then
    . $XDBMS_ENV_FILE2

    if [ $cp_old_dbtxt = "yes" ]; then
        rm -f $target_dir/databases/databases.txt
        cp $target_dir/CUBRID_DATABASES/databases.txt $target_dir/databases
    fi

    if [ $is_protego = "no" ]; then
        if [ -r $CUBRID/demo/make_cubrid_demo.sh ]; then
#       echo 'Create demodb...'
            (mkdir -p $CUBRID_DATABASES/demodb ; cd $CUBRID_DATABASES/demodb ; $CUBRID/demo/make_cubrid_demo.sh > /dev/null 2>&1)
            if [ $? = 0 ]; then
                echo "demodb has been successfully created."
            else
                echo "Cannot create demodb."
            fi
        fi
    fi
fi

echo ""
echo "If you want to use ${PRODUCT_NAME}, run the following commands"

case $SHELL in
    */csh | */tcsh )
        echo "  % source $XDBMS_ENV_FILE1"
    ;;
    * )
        echo "  % . $XDBMS_ENV_FILE2"
    ;;
esac

echo "  % cubrid service start"
echo ""
#if (${?demodb} && $demodb == 'true')  then
#       echo 'If you want to start up demodb, run the following commans'
#       echo "  % start_server demodb"
#endif

#./confcp.sh $target_dir
targets=$(ls $target_dir/conf/*-dist)

for arg in $targets
do
    file=${arg%%-dist}
    # save old conf file
    if [ -f $file ]; then
      mv -f "$file" "$file".save > /dev/null 2>&1
    fi
    mv -f "$file"-dist "$file" > /dev/null 2>&1
done

cp -f COPYING $CUBRID
END_OF_FILE
}


function print_check_glibc_version_c ()
{
cat << \END_OF_FILE
#include <stdio.h>
#include <gnu/libc-version.h>

int build_version = BUILD_VERSION;

int (main)(void)
{
    const char *v = gnu_get_libc_version();
    if (v == NULL)
        return 1;

    switch (build_version) {
    case 232:
        if (strcmp(v, "2.3.2") == 0)
            return 0;
        break;
    case 234:
        if (strcmp(v, "2.3.4") >= 0)
            return 0;
        break;
    }

    return 1;
}

END_OF_FILE
}


function build_bin_pack ()
{
  if [ $# -lt 2 ]; then
    print_error "Missing product name or target"
    return 1
  else
    package_file="$1"
    pack_target="$2"
  fi

  archive_dir="$install_dir/$product_name"
  if [ "$prefix_dir" != "$archive_dir" ]; then
    cp -rf $prefix_dir $archive_dir
  fi

  if [ "$pack_target" = "tarball" ]; then
    (cd $install_dir && tar czf $package_file $product_name)
    return $?
  elif [ "$pack_target" != "shell" ]; then
    print_error "Unknown target"
    return 1
  fi

  # prepare extra files
  cp $source_dir/COPYING $install_dir
  print_install_sh > $install_dir/CUBRID_Install.sh
  chmod a+x $install_dir/CUBRID_Install.sh
  print_setup_sh > $install_dir/CUBRID_Setup.sh
  chmod a+x $install_dir/CUBRID_Setup.sh
  print_check_glibc_version_c > $install_dir/check_glibc_version.c
  if ! gcc -o $install_dir/check_glibc_version -DBUILD_VERSION=234 $install_dir/check_glibc_version.c; then
    print_fatal "Check_glibc_version build error"
  fi

  echo "version=\"$major.$minor\"" > $install_dir/version.sh
  echo "BuildNumber=$build_number" >> $install_dir/version.sh

  conf_files=$(ls $archive_dir/conf/*.conf $archive_dir/conf/*.pass $archive_dir/conf/*.txt 2> /dev/null)
  for file in $conf_files; do
    if [ -f $file ]; then
      mv -f $file $file-dist
    fi
    if [ ! -f $file-dist ]; then
      print_fatal "Config file [$file-dist] not found"
    fi
  done

  (cd $install_dir/$product_name &&
    tar zcf $install_dir/CUBRID-product.tar.gz * &&
    cd $install_dir && mv -f CUBRID_Install.sh $package_file &&
    tar cf - CUBRID-product.tar.gz CUBRID_Setup.sh COPYING version.sh check_glibc_version >> $package_file)

  for file in $conf_files; do
    mv -f $file-dist $file
  done
}


function build_rpm ()
{
  print_check "Preparing RPM package directory"

  if [ $# -lt 2 ]; then
    print_error "Missing source tarball filename or target"
    return 1
  else
    source_tarball="$1"
    rpm_target="$2"
  fi

  if [ ! -f "$source_tarball" ]; then
    print_error "Source tarball [$source_tarball] is not exist"
    return 1
  else
    print_info "Using source tarball [$source_tarball]"
    rpm_output_dir=$(dirname $source_tarball)
  fi

  mkdir -p $install_dir/rpmbuild/{BUILD,RPMS,SPECS,SOURCES,SRPMS}
  print_result "OK"

  case $rpm_target in
    srpm)
      rpmbuild --define="_topdir $install_dir/rpmbuild" --clean -ts $source_tarball
      [ $? -eq 0 ] && mv -f $install_dir/rpmbuild/SRPMS/$product_name_lower-$build_number-*.src.rpm $rpm_output_dir
    ;;
    rpm)
      rpmbuild --define="_topdir $install_dir/rpmbuild" --define="_tmppath $install_dir/rpmbuild/tmp" --clean -tb --target=$build_target $source_tarball
      [ $? -eq 0 ] && mv -f $install_dir/rpmbuild/RPMS/$build_target/$product_name_lower-$build_number-*.$build_target.rpm $rpm_output_dir
    ;;
    *)
      print_error "Unknown target"
      return 1
      ;;
  esac
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

  if [ ! -d $output_dir ]; then
    mkdir -p $output_dir
  fi
  
  # create additional dirs for binary package
  pre_created_dirs="databases var var/log var/tmp var/run var/lock var/manager log log/manager"
  for dir in $pre_created_dirs; do
    mkdir -p "$prefix_dir/$dir"
  done

  # copy files for binary package
  pre_installed_files="COPYING README CREDITS"
  for file in $pre_installed_files; do
    cp $source_dir/$file "$prefix_dir"
  done

  src_package_name="$product_name_lower-$build_number.tar.gz"
  print_result "OK"

  for package in $packages; do
    print_check "Packing package for $package"
    case $package in
      src)
	if [ ! "$build_mode" = "release" ]; then
	  print_info "$build_mode mode source tarball is not supported. Skip"
	  package_name="NONE"
	else
	  package_name="$src_package_name"
	  # make dist for pack sources
	  (cd $build_dir && make dist)
	  if [ $? -eq 0 ]; then
	    output_packages="$output_packages $package_name"
	    [ $build_dir -ef $output_dir ] || mv -f $build_dir/$package_name $output_dir
	  else
	    false
	  fi
	fi
      ;;
      zip_src)
        if [ ! "$build_mode" = "release" ]; then
	  print_info "$build_mode mode source zip is not supported. Skip"
	  package_name="NONE"
	else
	  package_name="$product_name_lower-$build_number.zip"
	  # make dist-zip for pack sources
	  (cd $build_dir && make dist-zip)
	  if [ $? -eq 0 ]; then
	    output_packages="$output_packages $package_name"
	    [ $build_dir -ef $output_dir ] || mv -f $build_dir/$package_name $output_dir
	  else
	    false
	  fi
	fi
      ;;
      cci_src)
	if [ ! "$build_mode" = "release" ]; then
	  print_info "$build_mode mode cci source tarball is not supported. Skip"
	  package_name="NONE"
	else
	  package_basename="$product_name_lower-cci"
	  package_name="$package_basename-$build_number.tar.gz"
	  # make dist for pack cci sources
	  if [ -d "$build_dir/$package_basename" ]; then
	    rm -rf $build_dir/$package_basename
	  fi
	  mkdir $build_dir/$package_basename
	  if [ "$(readlink -f $build_dir/..)" = "$source_dir" ]; then
	    configure_dir="../.."
	  else
	    configure_dir="$source_dir"
	  fi
	  (cd $build_dir/$package_basename && $configure_dir/configure --with-cci-only && make PACKAGE=$package_basename dist && mv $package_name $build_dir)
	  if [ $? -eq 0 ]; then
	    output_packages="$output_packages $package_name"
	    [ $build_dir -ef $output_dir ] || mv -f $build_dir/$package_name $output_dir
	  else
	    false
	  fi
	fi
      ;;
	php_src)
	  if [ ! "$build_mode" = "release" ]; then
	  print_info "$build_mode mode php source tarball is not supported. Skip"
	  package_name="NONE"
	else
	  package_basename="$product_name-php-$build_number"
	  package_name="$package_basename.tar.gz"
	  if [ -d "$build_dir/$package_basename" ]; then
	    rm -rf $build_dir/$package_basename
	  fi
	  mkdir $build_dir/$package_basename
	  cp -r $source_dir/contrib/php* $build_dir/$package_basename/
	  (cd $build_dir && tar czf $output_dir/$package_name $package_basename)
	  if [ $? -eq 0 ]; then
	    output_packages="$output_packages $package_name"
	    rm -rf $build_dir/$package_basename
	  else
	    false
	  fi
	fi
      ;;
      tarball|shell)
	if [ ! -d "$install_dir" -o ! -d "$prefix_dir" ]; then
	  print_fatal "Installed directory or prefix directory not found"
	fi

      	package_basename="$product_name-$build_number-linux.$build_target"
	if [ ! "$build_mode" = "release" ]; then
	  package_basename="$package_basename-$build_mode"
	fi
	if [ "$package" = tarball ]; then
	  package_name="$package_basename.tar.gz"
	else
	  package_name="$package_basename.sh"
	fi
	if [ "$package" = "shell" -a "$without_cmserver" = "true" ]; then
	  print_info "CUBRID manager server is disabled. Skip shell package"
	else
	  build_bin_pack $output_dir/$package_name $package
	fi
	[ $? -eq 0 ] && output_packages="$output_packages $package_name"
      ;;
      cci)
	if [ ! -d "$install_dir" ]; then
	  print_fatal "Installed directory not found"
	fi

      	package_basename="$product_name-CCI-$build_number-$build_target"
	if [ ! "$build_mode" = "release" ]; then
	  package_name="$package_basename-$build_mode.tar.gz"
	else
	  package_name="$package_basename.tar.gz"
	fi
	cci_headers="include/cas_cci.h include/cas_error.h"
	cci_libs="lib/libcascci.a lib/libcascci.so*"
	for file in $cci_headers $cci_libs; do
	  pack_file_list="$pack_file_list $product_name/$file"
	done
	(cd $install_dir && tar czf $output_dir/$package_name $pack_file_list)
	[ $? -eq 0 ] && output_packages="$output_packages $package_name"
      ;;
      jdbc)
	if [ ! "$build_mode" = "release" ]; then
	  print_info "$build_mode mode JDBC is not supported. Skip"
	  package_name="NONE"
	else
	  package_name="JDBC-$build_number-$product_name_lower"
	  jar_files=$(cd $build_dir/jdbc && ls $package_name*.jar)
	  cp $build_dir/jdbc/$package_name*.jar $output_dir
	  [ $? -eq 0 ] && output_packages="$output_packages $jar_files"
	fi
      ;;
      srpm)
	if [ "$without_cmserver" = "true" ]; then
	  print_info "CUBRID manager server is disabled. Skip SRPM build"
	elif [ ! "$build_mode" = "release" ]; then
	  print_info "$build_mode mode SRPM is not supported. Skip"
	  package_name="NONE"
	else
	  package_name="$product_name-$build_number...src.rpm"
	  build_rpm $output_dir/$src_package_name $package
	  if [ $? -eq 0 ]; then
	    rpm_pkgs=$(cd $output_dir && ls $product_name_lower-$build_number-*.src.rpm)
	    [ $? -eq 0 ] && output_packages="$output_packages $rpm_pkgs"
	  else
	    false
	  fi
	fi
      ;;
      rpm)
	if [ "$without_cmserver" = "true" ]; then
	  print_info "CUBRID manager server is disabled. Skip RPM build"
	elif [ ! "$build_mode" = "release" ]; then
	  print_info "$build_mode mode RPM or SRPM is not supported. Skip"
	  package_name="NONE"
	else
	  package_name="$product_name-$build_number...rpm"
	  build_rpm $output_dir/$src_package_name $package
	  if [ $? -eq 0 ]; then
	    rpm_pkgs=$(cd $output_dir && ls $product_name_lower-$build_number-*.$build_target.rpm)
	    [ $? -eq 0 ] && output_packages="$output_packages $rpm_pkgs"
	  else
	    false
	  fi
	fi
      ;;
      owfs)
	package_basename="$product_name-owfs-$build_number-$build_target"
	if [ ! "$build_mode" = "release" ]; then
	  package_name="$package_basename-$build_mode.tar.gz"
	else
	  package_name="$package_basename.tar.gz"
	fi
	owfs_build_dir="$build_dir/$package_basename"
	owfs_install_dir="$owfs_build_dir/$package_basename"
	if [ -d "$owfs_build_dir" ]; then
	  rm -rf $owfs_build_dir
	fi
	mkdir $owfs_build_dir
	mkdir $owfs_install_dir
	if [ "$(readlink -f $build_dir/..)" = "$source_dir" ]; then
	  configure_dir="../.."
	else
	  configure_dir="$source_dir"
	fi
	(cd $owfs_build_dir && $configure_dir/configure --prefix=$owfs_install_dir --enable-owfs $configure_options && make -j && make install)
	if [ $? -eq 0 ]; then
	  (cd $owfs_build_dir && tar czf $output_dir/$package_name $package_basename)
	  [ $? -eq 0 ] && output_packages="$output_packages $package_name"
	else
	  false
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
  echo "  Version [$build_number]"
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
  echo "  -z arg  Package to generate (src,zip_src,cci_src,php_src,shell,tarball,cci,jdbc,srpm,rpm,owfs);"
  echo "          [default: all]"
  echo "  -? | -h Show this help message and exit"
  echo ""
  echo " TARGET"
  echo "  all     Build and create packages (default)"
  echo "  prepare Prepare only (check increase version and autogen option)"
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
  while getopts ":t:m:is:b:p:o:aj:c:z:h" opt; do
    case $opt in
      t ) build_target="$OPTARG" ;;
      m ) build_mode="$OPTARG" ;;
      i ) increase_build_number="yes" ;;
      s ) source_dir="$OPTARG" ;;
      b ) build_dir="$OPTARG" ;;
      p ) prefix_dir="$OPTARG" ;;
      o ) output_dir="$OPTARG" ;;
      a ) run_autogen="yes" ;;
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
  install_dir="$build_dir/_install"
  [ ! -d "$install_dir" ] && mkdir -p $install_dir

  if [ "x$prefix_dir" = "x" ]; then
    prefix_dir="$install_dir/$product_name"
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
    packages="src zip_src cci_src php_src tarball shell cci jdbc srpm rpm owfs"
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
    print_error "Pakcages with coverage mode is not supported. Skip"
    return 0
  fi
  # check coverage mode in configure option
  case "$configure_options" in
    *"--enable-coverage"*)
      print_error "Pakcages with coverage mode is not supported. Skip"
      return 0
  esac

  build_package
}


function build_prepare ()
{
  build_increase_version && build_autogen
}


function build_build ()
{
  build_prepare && build_configure && build_compile && build_install
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
    build_args="clean build dist"
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
