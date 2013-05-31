
%define cubrid_version 10.0.0.0059
%define build_version  10.0.0
%define cubrid_vendor  Search Solution Corporation
%define release        el5
%define cubrid_user    cubrid

Summary:       An open source database highly optimized for Web applications
Name:          cubrid
Version:       %{cubrid_version}
Release:       %{release}%{?dist}
Provides:      CUBRID = 2:%{version}-%{release}
Obsoletes:     CUBRID < 2:8.4.3
License:       GPLv2+ and BSD
Group:         Applications/Databases
URL:           http://www.cubrid.org
Source0:       %{name}-%{cubrid_version}.tar.gz
Requires:      ncurses libstdc++ libgcrypt coreutils /sbin/chkconfig /usr/sbin/useradd /usr/sbin/groupadd
BuildRequires: gcc-c++ elfutils-libelf-devel ncurses-devel libstdc++-devel glibc-devel boost-devel
BuildRequires: ant >= 0:1.6.0 java-devel >= 0:1.5.0 libgcrypt-devel
BuildRoot:     %{_tmppath}/%{name}-%{version}-build
Prefix:        %{_prefix}
AutoReq:       no

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

install -d %{buildroot}%{_sysconfdir}/{profile.d,init.d}
install -c -m 644 contrib/rpm/cubrid.sh %{buildroot}%{_sysconfdir}/profile.d/cubrid.sh
install -c -m 644 contrib/rpm/cubrid.csh %{buildroot}%{_sysconfdir}/profile.d/cubrid.csh
install -c -m 755 contrib/init.d/cubrid %{buildroot}%{_sysconfdir}/init.d/cubrid
install -c -m 755 contrib/init.d/cubrid-ha %{buildroot}%{_sysconfdir}/init.d/cubrid-ha

%pre
# delete the cubrid group if no cubrid user is found, before adding the user
if [ -z "`getent passwd %{cubrid_user}`" ] && ! [ -z "`getent group %{cubrid_user}`" ]; then
    %{_sbindir}/groupdel %{cubrid_user} 2> /dev/null || :
fi

if [ -z "`getent passwd %{cubrid_user}`" ]; then
    mkdir -p /opt/cubrid
    groupadd -r %{cubrid_user} || true
    useradd -g %{cubrid_user} -d /opt/cubrid -M -s /bin/bash -c "CUBRID user" %{cubrid_user} || true
fi

mkdir -p /opt/cubrid/log
mkdir -p /opt/cubrid/var
mkdir -p /opt/cubrid/tmp
mkdir -p /opt/cubrid/databases
chown cubrid:cubrid -R /opt/cubrid


%post
# Make cubrid start/shutdown automatically.
if [ -x /sbin/chkconfig ] ; then
	/sbin/chkconfig --add cubrid
fi

%preun
# Stop cubrid before uninstalling it
if [ -x %{_sysconfdir}/init.d/cubrid ] ; then
	%{_sysconfdir}/init.d/cubrid stop > /dev/null
	# Remove autostart of cubrid
	if [ -x /sbin/chkconfig ] ; then
		/sbin/chkconfig --del cubrid
	fi
fi

%postun
# All user's data and user should not remove.
#userdel cubrid

#rm -rf /opt/cubrid/log
#rm -rf /opt/cubrid/var
#rm -rf /opt/cubrid/tmp
#rm -rf /opt/cubrid/databases


%clean
rm -rf %{buildroot}


%files
%defattr(-,root,root)
/etc/profile.d
%defattr(-,%{cubrid_user},%{cubrid_user})
/opt/cubrid/bin
/opt/cubrid/compat
/opt/cubrid/demo
/opt/cubrid/include
/opt/cubrid/java
/opt/cubrid/jdbc
/opt/cubrid/lib
%exclude /opt/cubrid/lib/*.la
/opt/cubrid/locales
/opt/cubrid/log
/opt/cubrid/msg
/opt/cubrid/share
/opt/cubrid/tmp
/opt/cubrid/var
%config(noreplace) /opt/cubrid/conf
%attr(755, root, root) %{_sysconfdir}/init.d/cubrid
%attr(755, root, root) %{_sysconfdir}/init.d/cubrid-ha


%changelog
* Mon Apr 23 2013 Hyunwook Kim <hwkim@nhn.com>
- Register a service daemon

* Fri May 25 2012 Siwan Kim <siwankim@nhn.com>
- Fix informations - Summary, Name, License,...

* Sun Jun 28 2009 Siwan Kim <siwankim@nhn.com>
- Renewal for 8.2.0

* Mon Oct 06 2008 Siwan Kim <siwankim@nhn.com>
- Initial import of spec.
