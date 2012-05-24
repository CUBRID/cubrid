
%define cubrid_version 8.4.9.0257
%define build_version  8.4.9
%define cubrid_vendor  Search Solution Corporation
%define release        el5
%define cubrid_user    cubrid

Summary:       CUBRID: a very fast and reliable SQL database server
Name:          CUBRID
Version:       %{cubrid_version}
Release:       %{release}%{?dist}
License:       GPLv2+/BSD
Group:         Applications/Databases
URL:           http://www.cubrid.com
Source0:       cubrid-%{cubrid_version}.tar.gz
Provides:      cubrid
Vendor:        %{cubrid_vendor}
Packager:      cubrid developer <cubrid@nhn.com>
Requires:      ncurses
Requires:      libstdc++
BuildRequires: gcc-c++
BuildRequires: elfutils-libelf-devel
BuildRequires: ncurses-devel
BuildRequires: libstdc++-devel
BuildRequires: glibc-devel
BuildRequires: boost-devel
BuildRoot:     %{_tmppath}/%{name}-%{version}-build
Prefix:        %{_prefix}

%description -l ko
Please see the documentation and the manual
or visit http://www.cubrid.com/developer.cub for more information.
%description
Please see the documentation and the manual
or visit http://www.cubrid.org/documentation for more information.

%prep
%setup -q -n cubrid-%{version}


%build
CUBRID_COMMON_CONFIGURE="--prefix=%{buildroot}/opt/cubrid"
%ifarch x86_64
    CUBRID_COMMON_CONFIGURE="${CUBRID_COMMON_CONFIGURE} --enable-64bit"
%endif

./configure $CUBRID_COMMON_CONFIGURE
make -j

%install
rm -rf %{buildroot}

make install

install -d %{buildroot}%{_sysconfdir}/profile.d
install -c -m 755 contrib/rpm/cubrid.sh %{buildroot}%{_sysconfdir}/profile.d/cubrid.sh
install -c -m 755 contrib/rpm/cubrid.csh %{buildroot}%{_sysconfdir}/profile.d/cubrid.csh

# include
#install -d %{buildroot}%{_includedir}
#install -c -m 644 src/cci/cas_cci.h %{buildroot}%{_includedir}/cas_cci.h
#install -c -m 644 src/executables/cubrid_esql.h %{buildroot}%{_includedir}/cubrid_esql.h
#install -c -m 644 src/compat/dbi_compat.h %{buildroot}%{_includedir}/dbi.h

# lib
#install -d %{buildroot}%{_libdir}

# lib-cas
#install -c -m 644 cas/libcas.a %{buildroot}%{_libdir}/libcas.a
#ranlib %{buildroot}%{_libdir}/libcas.a
#install -c -m 755 cas/.libs/libcascci.so.%{build_version} %{buildroot}%{_libdir}/libcascci.so.%{build_version}
#pushd %{buildroot}%{_libdir}
#	ln -s -f libcascci.so.%{build_version} libcascci.so.8
#	ln -s -f libcascci.so.%{build_version} libcascci.so
#popd
#install -c -m 644 cas/.libs/libcascci.a %{buildroot}%{_libdir}/libcascci.a
#ranlib %{buildroot}%{_libdir}/libcascci.a

# lib-sa
#install -c -m 755 sa/.libs/libcubridsa.so.%{build_version} %{buildroot}%{_libdir}/libcubridsa.so.%{build_version}
#pushd %{buildroot}%{_libdir}
#	ln -s -f libcubridsa.so.%{build_version} libcubridsa.so.8
#	ln -s -f libcubridsa.so.%{build_version} libcubridsa.so
#popd
#install -c -m 644 sa/.libs/libcubridsa.a %{buildroot}%{_libdir}/libcubridsa.a
#ranlib %{buildroot}%{_libdir}/libcubridsa.a

# lib-cs
#install -c -m 755 cs/.libs/libcubridcs.so.%{build_version} %{buildroot}%{_libdir}/libcubridcs.so.%{build_version}
#pushd %{buildroot}%{_libdir}
#	ln -s -f libcubridcs.so.%{build_version} libcubridcs.so.8
#	ln -s -f libcubridcs.so.%{build_version} libcubridcs.so
#popd
#install -c -m 644 cs/.libs/libcubridcs.a %{buildroot}%{_libdir}/libcubridcs.a
#ranlib %{buildroot}%{_libdir}/libcubridcs.a

# lib-cubrid
#install -c -m 755 cubrid/.libs/libcubrid.so.%{build_version} %{buildroot}%{_libdir}/libcubrid.so.%{build_version}
#pushd %{buildroot}%{_libdir}
#	ln -s -f libcubrid.so.%{build_version} libcubrid.so.8
#	ln -s -f libcubrid.so.%{build_version} libcubrid.so
#popd
#install -c -m 644 cubrid/.libs/libcubrid.a %{buildroot}%{_libdir}/libcubrid.a
#ranlib %{buildroot}%{_libdir}/libcubrid.a

# lib-util
#install -c -m 755 util/.libs/libcubridesql.so.%{build_version} %{buildroot}%{_libdir}/libcubridesql.so.%{build_version}
#pushd %{buildroot}%{_libdir}
#	ln -s -f libcubridesql.so.%{build_version} libcubridesql.so.8
#	ln -s -f libcubridesql.so.%{build_version} libcubridesql.so
#popd
#install -c -m 644 util/.libs/libcubridesql.a %{buildroot}%{_libdir}/libcubridesql.a
#ranlib %{buildroot}%{_libdir}/libcubridesql.a

# bin
#install -d %{buildroot}%{_bindir}

# bin-util
#install -c -m 755 util/csql %{buildroot}%{_bindir}/csql
#install -c -m 755 util/cub_admin %{buildroot}%{_bindir}/cub_admin
#install -c -m 755 util/.libs/cub_master %{buildroot}%{_bindir}/cub_master
#install -c -m 755 util/.libs/cub_server %{buildroot}%{_bindir}/cub_server
#install -c -m 755 util/.libs/cubrid %{buildroot}%{_bindir}/cubrid
#install -c -m 755 util/.libs/cub_commdb %{buildroot}%{_bindir}/cub_commdb
#install -c -m 755 util/.libs/cubrid_esql %{buildroot}%{_bindir}/cubrid_esql
#install -c -m 755 util/.libs/cubrid_rel %{buildroot}%{_bindir}/cubrid_rel
#install -c -m 755 util/.libs/loadjava %{buildroot}%{_bindir}/loadjava
#install -c -m 755 util/.libs/migrate_r22 %{buildroot}%{_bindir}/migrate_r22

# bin-broker
#install -c -m 755 broker/.libs/cub_broker %{buildroot}%{_bindir}/cub_broker
#install -c -m 755 broker/.libs/cubrid_broker %{buildroot}%{_bindir}/cubrid_broker
#install -c -m 755 broker/.libs/cub_cas %{buildroot}%{_bindir}/cub_cas
#install -c -m 755 broker/.libs/broker_monitor %{buildroot}%{_bindir}/broker_monitor
#install -c -m 755 broker/.libs/broker_changer %{buildroot}%{_bindir}/broker_changer
#install -c -m 755 broker/.libs/broker_log_converter %{buildroot}%{_bindir}/broker_log_converter
#install -c -m 755 broker/.libs/broker_log_runner %{buildroot}%{_bindir}/broker_log_runner
#install -c -m 755 broker/.libs/broker_log_top %{buildroot}%{_bindir}/broker_log_top

# bin-cmserver
#install -c -m 755 cmserver/cub_js %{buildroot}%{_bindir}/cub_js
#install -c -m 755 cmserver/.libs/cub_job %{buildroot}%{_bindir}/cub_job
#install -c -m 755 cmserver/.libs/cub_jobsa %{buildroot}%{_bindir}/cub_jobsa
#install -c -m 755 cmserver/.libs/cub_auto %{buildroot}%{_bindir}/cub_auto
#install -c -m 755 cmserver/.libs/cub_sainfo %{buildroot}%{_bindir}/cub_sainfo

# conf
#install -d %{buildroot}%{_sysconfdir}/cubrid

# conf-cubrid
#install -c -m 644 conf/cubrid.conf %{buildroot}%{_sysconfdir}/cubrid/cubrid.conf
#install -c -m 644 conf/cubrid_broker.conf %{buildroot}%{_sysconfdir}/cubrid/cubrid_broker.conf

# conf-cmserver
#install -c -m 644 cmserver/conf/cm.conf %{buildroot}%{_sysconfdir}/cubrid/cm.conf
#install -c -m 644 cmserver/conf/cm.pass %{buildroot}%{_sysconfdir}/cubrid/cm.pass
#install -c -m 644 cmserver/conf/cmdb.pass %{buildroot}%{_sysconfdir}/cubrid/cmdb.pass
#install -c -m 644 cmserver/conf/diagactivitytemplate.conf %{buildroot}%{_sysconfdir}/cubrid/diagactivitytemplate.conf
#install -c -m 644 cmserver/conf/diagstatustemplate.conf %{buildroot}%{_sysconfdir}/cubrid/diagstatustemplate.conf
#install -c -m 644 cmserver/conf/autoaddvoldb.conf %{buildroot}%{_sysconfdir}/cubrid/autoaddvoldb.conf
#install -c -m 644 cmserver/conf/autobackupdb.conf %{buildroot}%{_sysconfdir}/cubrid/autobackupdb.conf
#install -c -m 644 cmserver/conf/autoexecquery.conf %{buildroot}%{_sysconfdir}/cubrid/autoexecquery.conf
#install -c -m 644 cmserver/conf/autohistory.conf %{buildroot}%{_sysconfdir}/cubrid/autohistory.conf

# data
#install -d %{buildroot}%{_datadir}/cubrid

# data-msg
#install -d %{buildroot}%{_datadir}/cubrid/msg

# data-msg-en_US
#install -d %{buildroot}%{_datadir}/cubrid/msg/en_US
#install -c -m 644 msg/en_US/cubrid.msg %{buildroot}%{_datadir}/cubrid/msg/en_US/cubrid.msg
#install -c -m 644 msg/en_US/cubrid.cat %{buildroot}%{_datadir}/cubrid/msg/en_US/cubrid.cat
#install -c -m 644 msg/en_US/csql.msg %{buildroot}%{_datadir}/cubrid/msg/en_US/csql.msg
#install -c -m 644 msg/en_US/csql.cat %{buildroot}%{_datadir}/cubrid/msg/en_US/csql.cat
#install -c -m 644 msg/en_US/utils.msg %{buildroot}%{_datadir}/cubrid/msg/en_US/utils.msg
#install -c -m 644 msg/en_US/utils.cat %{buildroot}%{_datadir}/cubrid/msg/en_US/utils.cat
#install -c -m 644 msg/en_US/esql.msg %{buildroot}%{_datadir}/cubrid/msg/en_US/esql.msg
#install -c -m 644 msg/en_US/esql.cat %{buildroot}%{_datadir}/cubrid/msg/en_US/esql.cat
#install -c -m 644 msg/en_US/syntax.txt %{buildroot}%{_datadir}/cubrid/msg/en_US/syntax.txt

# data-msg-en_US.utf8
#install -d %{buildroot}%{_datadir}/cubrid/msg/en_US.utf8
#install -c -m 644 msg/en_US.utf8/cubrid.msg %{buildroot}%{_datadir}/cubrid/msg/en_US.utf8/cubrid.msg
#install -c -m 644 msg/en_US.utf8/cubrid.cat %{buildroot}%{_datadir}/cubrid/msg/en_US.utf8/cubrid.cat
#install -c -m 644 msg/en_US.utf8/csql.msg %{buildroot}%{_datadir}/cubrid/msg/en_US.utf8/csql.msg
#install -c -m 644 msg/en_US.utf8/csql.cat %{buildroot}%{_datadir}/cubrid/msg/en_US.utf8/csql.cat
#install -c -m 644 msg/en_US.utf8/utils.msg %{buildroot}%{_datadir}/cubrid/msg/en_US.utf8/utils.msg
#install -c -m 644 msg/en_US.utf8/utils.cat %{buildroot}%{_datadir}/cubrid/msg/en_US.utf8/utils.cat
#install -c -m 644 msg/en_US.utf8/esql.msg %{buildroot}%{_datadir}/cubrid/msg/en_US.utf8/esql.msg
#install -c -m 644 msg/en_US.utf8/esql.cat %{buildroot}%{_datadir}/cubrid/msg/en_US.utf8/esql.cat
#install -c -m 644 msg/en_US.utf8/syntax.txt %{buildroot}%{_datadir}/cubrid/msg/en_US.utf8/syntax.txt

# data-msg-ko_KR.euckr
#install -d %{buildroot}%{_datadir}/cubrid/msg/ko_KR.euckr
#install -c -m 644 msg/ko_KR.euckr/cubrid.msg %{buildroot}%{_datadir}/cubrid/msg/ko_KR.euckr/cubrid.msg
#install -c -m 644 msg/ko_KR.euckr/cubrid.cat %{buildroot}%{_datadir}/cubrid/msg/ko_KR.euckr/cubrid.cat
#install -c -m 644 msg/ko_KR.euckr/csql.msg %{buildroot}%{_datadir}/cubrid/msg/ko_KR.euckr/csql.msg
#install -c -m 644 msg/ko_KR.euckr/csql.cat %{buildroot}%{_datadir}/cubrid/msg/ko_KR.euckr/csql.cat
#install -c -m 644 msg/ko_KR.euckr/utils.msg %{buildroot}%{_datadir}/cubrid/msg/ko_KR.euckr/utils.msg
#install -c -m 644 msg/ko_KR.euckr/utils.cat %{buildroot}%{_datadir}/cubrid/msg/ko_KR.euckr/utils.cat
#install -c -m 644 msg/ko_KR.euckr/esql.msg %{buildroot}%{_datadir}/cubrid/msg/ko_KR.euckr/esql.msg
#install -c -m 644 msg/ko_KR.euckr/esql.cat %{buildroot}%{_datadir}/cubrid/msg/ko_KR.euckr/esql.cat
#install -c -m 644 msg/ko_KR.euckr/syntax.txt %{buildroot}%{_datadir}/cubrid/msg/ko_KR.euckr/syntax.txt

# data-msg-ko_KR.utf8
#install -d %{buildroot}%{_datadir}/cubrid/msg/ko_KR.utf8
#install -c -m 644 msg/ko_KR.utf8/cubrid.msg %{buildroot}%{_datadir}/cubrid/msg/ko_KR.utf8/cubrid.msg
#install -c -m 644 msg/ko_KR.utf8/cubrid.cat %{buildroot}%{_datadir}/cubrid/msg/ko_KR.utf8/cubrid.cat
#install -c -m 644 msg/ko_KR.utf8/csql.msg %{buildroot}%{_datadir}/cubrid/msg/ko_KR.utf8/csql.msg
#install -c -m 644 msg/ko_KR.utf8/csql.cat %{buildroot}%{_datadir}/cubrid/msg/ko_KR.utf8/csql.cat
#install -c -m 644 msg/ko_KR.utf8/utils.msg %{buildroot}%{_datadir}/cubrid/msg/ko_KR.utf8/utils.msg
#install -c -m 644 msg/ko_KR.utf8/utils.cat %{buildroot}%{_datadir}/cubrid/msg/ko_KR.utf8/utils.cat
#install -c -m 644 msg/ko_KR.utf8/esql.msg %{buildroot}%{_datadir}/cubrid/msg/ko_KR.utf8/esql.msg
#install -c -m 644 msg/ko_KR.utf8/esql.cat %{buildroot}%{_datadir}/cubrid/msg/ko_KR.utf8/esql.cat

# data-msg-tr_TR.utf8
#install -d %{buildroot}%{_datadir}/cubrid/msg/en_US.utf8
#install -c -m 644 msg/tr_TR.utf8/cubrid.msg %{buildroot}%{_datadir}/cubrid/msg/tr_TR.utf8/cubrid.msg
#install -c -m 644 msg/tr_TR.utf8/cubrid.cat %{buildroot}%{_datadir}/cubrid/msg/tr_TR.utf8/cubrid.cat
#install -c -m 644 msg/tr_TR.utf8/csql.msg %{buildroot}%{_datadir}/cubrid/msg/tr_TR.utf8/csql.msg
#install -c -m 644 msg/tr_TR.utf8/csql.cat %{buildroot}%{_datadir}/cubrid/msg/tr_TR.utf8/csql.cat
#install -c -m 644 msg/tr_TR.utf8/utils.msg %{buildroot}%{_datadir}/cubrid/msg/tr_TR.utf8/utils.msg
#install -c -m 644 msg/tr_TR.utf8/utils.cat %{buildroot}%{_datadir}/cubrid/msg/tr_TR.utf8/utils.cat
#install -c -m 644 msg/tr_TR.utf8/esql.msg %{buildroot}%{_datadir}/cubrid/msg/tr_TR.utf8/esql.msg
#install -c -m 644 msg/tr_TR.utf8/esql.cat %{buildroot}%{_datadir}/cubrid/msg/tr_TR.utf8/esql.cat
#install -c -m 644 msg/tr_TR.utf8/syntax.txt %{buildroot}%{_datadir}/cubrid/msg/tr_TR.utf8/syntax.txt

# data-jdbc
#install -d %{buildroot}%{_datadir}/cubrid/jdbc
#install -c -m 644 jdbc/JDBC-%{cubrid_version}.jar %{buildroot}%{_datadir}/cubrid/jdbc/cubrid_jdbc.jar

# data-java
#install -d %{buildroot}%{_datadir}/cubrid/java
#install -c -m 644 java/jspserver.jar %{buildroot}%{_datadir}/cubrid/java/jspserver.jar
#install -c -m 644 src/jdbc/logging.properties %{buildroot}%{_datadir}/cubrid/java/logging.properties

# data-demo
#install -d %{buildroot}%{_datadir}/cubrid/demo
#install -c -m 755 demo/make_cubrid_demo.sh %{buildroot}%{_datadir}/cubrid/demo/make_cubrid_demo.sh
#install -c -m 644 demo/demodb_schema %{buildroot}%{_datadir}/cubrid/demo/demodb_schema
#install -c -m 644 demo/demodb_objects %{buildroot}%{_datadir}/cubrid/demo/demodb_objects


%pre
# delete the cubrid group if no cubrid user is found, before adding the user
if [ -z "`getent passwd %{cubrid_user}`" ] && ! [ -z "`getent group %{cubrid_user}`" ]; then
    %{_sbindir}/groupdel %{cubrid_user} 2> /dev/null || :
fi

if [ -z "`getent passwd %{cubrid_user}`" ]; then
    groupadd -r %{cubrid_user} 
    useradd -g %{cubrid_user} -d /opt/cubrid -s /bin/bash -c "CUBRID user" %{cubrid_user}
fi

mkdir -p /opt/cubrid/log
mkdir -p /opt/cubrid/var
mkdir -p /opt/cubrid/tmp
mkdir -p /opt/cubrid/databases
chown cubrid:cubrid -R /opt/cubrid
#install -d /var/log/cubrid
#install -d /var/run/cubrid


%post
# change permissions
#chown -R %{cubrid_user}:%{cubrid_user} /var/lib/cubrid /var/run/cubrid /var/log/cubrid
#chmod 711 /var/lib/cubrid
#chmod 777 /var/log/cubrid
#chmod 777 /var/run/cubrid

# initialize
# create demodb
#if [ -f $CUBRID_DATABASES/databases.txt ];
#then
#	DEMODB=`grep demodb $CUBRID_DATABASES/databases.txt`
#	if [ "$DEMODB" != "" ];
#	then
#		echo "demodb has been created allready."
#	fi
#else
#	mkdir -p $CUBRID/databases/demodb
#	cd $CUBRID/databases/demodb
#	$CUBRID/bin/cub_admin createdb demodb &> /dev/null
#	if [ $? = 0 ];
#	then
#		$CUBRID/bin/cub_admin loaddb -u dba -s $CUBRID/demo/demodb_schema -d $CUBRID/demo/demodb_objects demodb &> /dev/null
#		if [ $? = 0 ];
#		then
#			echo "demodb has been successfully created."
#		else
#			echo "fail: loaddb"
#		fi
#	else
#		echo "fail: createdb"
#	fi
#fi

# edit environment
#PREFIX_DIR=%{_prefix}
#REG_EXP="s/\//\\\\\\//g"
#PREFIX_DIR=`echo "$PREFIX_DIR" | sed -e $REG_EXP`
#echo "PREFIX_DIR=$PREFIX_DIR"
#REG_EXP="'s/CUBRID_DIR/CUBRID=$PREFIX_DIR/g'"
#echo "sed -e $REG_EXP --in-place %{_sysconfdir}/profile.d/cubrid.sh"
#eval sed --in-place -e $REG_EXP %{_sysconfdir}/profile.d/cubrid.sh

#REG_EXP="'s/CUBRID_DIR/CUBRID\\ $PREFIX_DIR/g'"
#echo "sed -e $REG_EXP --in-place %{_sysconfdir}/profile.d/cubrid.csh"
#eval sed --in-place -e $REG_EXP %{_sysconfdir}/profile.d/cubrid.csh



%preun


%postun
#userdel cubrid
#groupdel cubrid

#rm -rf /var/lib/cubrid /var/log/cubrid /var/run/cubrid
#rm -rf /opt/cubrid/log
#rm -rf /opt/cubrid/var
#rm -rf /opt/cubrid/tmp
#rm -rf /opt/cubrid/databases

#rm %{_sysconfdir}/profile.d/cubrid.sh
#rm %{_sysconfdir}/profile.d/cubrid.csh


%clean
rm -rf %{buildroot}


%files
%defattr(-,root,root)
/etc/profile.d
%defattr(-,%{cubrid_user},%{cubrid_user})
/opt/cubrid
%config(noreplace) /opt/cubrid/conf/autoaddvoldb.conf
%config(noreplace) /opt/cubrid/conf/autobackupdb.conf
%config(noreplace) /opt/cubrid/conf/autoexecquery.conf
%config(noreplace) /opt/cubrid/conf/autohistory.conf
%config(noreplace) /opt/cubrid/conf/cm.conf
%config(noreplace) /opt/cubrid/conf/cmdb.pass
%config(noreplace) /opt/cubrid/conf/cm.pass
%config(noreplace) /opt/cubrid/conf/cubrid_broker.conf
%config(noreplace) /opt/cubrid/conf/cubrid.conf
%config(noreplace) /opt/cubrid/conf/diagactivitytemplate.conf
%config(noreplace) /opt/cubrid/conf/diagstatustemplate.conf

#/opt/cubrid/log
#/opt/cubrid/var
#/opt/cubrid/tmp

# bin
#%{_bindir}

# lib
#%attr(0755,root,root) %{_libdir}/*.a
#%attr(0755,root,root) %{_libdir}/*.so*

# include
#%{_includedir}

# conf
#%{_sysconfdir}/cubrid

# data
#%{_datadir}/cubrid/msg/en_US
#%{_datadir}/cubrid/msg/en_US.utf8
#%{_datadir}/cubrid/msg/ko_KR.euckr
#%{_datadir}/cubrid/msg/ko_KR.utf8
#%{_datadir}/cubrid/msg/tr_TR.utf8
#%{_datadir}/cubrid/jdbc
#%{_datadir}/cubrid/java
#%{_datadir}/cubrid/demo

%changelog
* Sun Jun 28 2009 Siwan Kim <siwankim@nhncorp.com>
- Renewal for 8.2.0

* Mon Oct 06 2008 Siwan Kim <siwankim@nhncorp.com>
- Initial import of spec.
