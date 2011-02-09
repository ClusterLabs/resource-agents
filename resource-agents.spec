#
# Copyright (c) 2009 SUSE LINUX Products GmbH, Nuernberg, Germany.
#
# All modifications and additions to the file contributed by third parties
# remain the property of their copyright owners, unless otherwise agreed
# upon. The license for this file, and modifications and additions to the
# file, is the same license as for the pristine package itself (unless the
# license for the pristine package is not an Open Source License, in which
# case the license is the MIT License). An "Open Source License" is a
# license that conforms to the Open Source Definition (Version 1.9)
# published by the Open Source Initiative.

# Please submit bugfixes or comments via http://bugs.opensuse.org/
#

# norootforbuild

# Directory where we install documentation
%if 0%{?fedora} || 0%{?centos_version} || 0%{?rhel}
%global agents_docdir %{_defaultdocdir}/%{name}-%{version}
%endif
%if 0%{?suse_version}
%global agents_docdir %{_defaultdocdir}/%{name}
%endif

# 
# Since this spec file supports multiple distributions, ensure we
# use the correct group for each.
#
%if 0%{?fedora} || 0%{?centos_version} || 0%{?rhel}
%define pkg_group System Environment/Daemons
%else
%define pkg_group Productivity/Clustering/HA
%endif
%define SSLeay		        perl-Net-SSLeay
%if 0%{?suse_version}
%define SSLeay			perl-Net_SSLeay
%endif

Name:           resource-agents
Summary:        Reusable cluster resource scripts
Version:        1.0.3
Release:        1%{?dist}
License:        GPL v2 or later; LGPL v2.1 or later
Url:            http://www.linux-ha.org
Group:		%{pkg_group}
Source:         resource-agents.tar.bz2
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
AutoReqProv:    on
Obsoletes:	heartbeat-resources
Conflicts:	heartbeat-resources
BuildRequires:  autoconf automake glib2-devel pkgconfig python-devel 
BuildRequires:  help2man

%if 0%{?suse_version}  
BuildRequires:  libnet libglue-devel
BuildRequires:  libxslt docbook_4 docbook-xsl-stylesheets
%endif

%if 0%{?fedora} || 0%{?centos_version} || 0%{?rhel}
BuildRequires:  which cluster-glue-libs-devel
BuildRequires:  libxslt docbook-dtds docbook-style-xsl
%endif

%description
Scripts to allow common services to operate in a High Availability environment.

%package -n ldirectord
License:        GPL v2 or later
Summary:        A Monitoring Daemon for Maintaining High Availability Resources
Group:          Productivity/Clustering/HA
Requires:       %{SSLeay} perl-libwww-perl ipvsadm
Provides:	heartbeat-ldirectord
Obsoletes:	heartbeat-ldirectord
Requires:	perl-MailTools
%if 0%{?suse_version}
Requires:       logrotate
%endif
%if 0%{?fedora_version}
Requires(post): /sbin/chkconfig
Requires(preun):/sbin/chkconfig
%endif

%description -n ldirectord
The Linux Director Daemon (ldirectord) was written by Jacob Rief.
<jacob.rief@tiscover.com>

ldirectord is a stand alone daemon for monitoring the services on real
servers. Currently, HTTP, HTTPS, and FTP services are supported.
lditrecord is simple to install and works with the heartbeat code
(http://www.linux-ha.org/).

See 'ldirectord -h' and linux-ha/doc/ldirectord for more information.

%prep
###########################################################
%setup -n resource-agents -q
###########################################################

%build
CFLAGS="${CFLAGS} ${RPM_OPT_FLAGS}"
export CFLAGS

./autogen.sh
%if 0%{?suse_version} >= 1020 || 0%{?fedora} >= 11 || 0%{?centos_version} > 5 || 0%{?rhel} > 5
%configure \
    --enable-fatal-warnings=yes \
    --with-package-name=%{name} \
    --docdir=%{agents_docdir}
%else
export docdir=%{agents_docdir}
%configure \
    --enable-fatal-warnings=yes \
    --with-package-name=%{name}
%endif


export MAKE="make %{?jobs:-j%jobs}"
make %{?jobs:-j%jobs}
###########################################################

%install
###########################################################
make DESTDIR=$RPM_BUILD_ROOT install
(
  mkdir -p $RPM_BUILD_ROOT/etc/ha.d/resource.d
  ln -s %{_sbindir}/ldirectord $RPM_BUILD_ROOT/etc/ha.d/resource.d/ldirectord
) || true
test -d $RPM_BUILD_ROOT/sbin || mkdir $RPM_BUILD_ROOT/sbin
(
  cd $RPM_BUILD_ROOT/sbin
  ln -sf /etc/init.d/ldirectord rcldirectord 
) || true

# Dont package static libs or compiled python
find $RPM_BUILD_ROOT -name '*.a' -type f -print0 | xargs -0 rm -f
find $RPM_BUILD_ROOT -name '*.la' -type f -print0 | xargs -0 rm -f
find $RPM_BUILD_ROOT -name '*.pyc' -type f -print0 | xargs -0 rm -f
find $RPM_BUILD_ROOT -name '*.pyo' -type f -print0 | xargs -0 rm -f

# Unset execute permissions from things that shouln't have it
find $RPM_BUILD_ROOT -name 'ocf-*'  -type f -print0 | xargs -0 chmod a-x
find $RPM_BUILD_ROOT -name '*.dtd'  -type f -print0 | xargs -0 chmod a-x
chmod 0755 $RPM_BUILD_ROOT/usr/sbin/ocf-tester
chmod 0755 $RPM_BUILD_ROOT/usr/sbin/ocft

(
cd $RPM_BUILD_ROOT/usr/lib/ocf/resource.d/heartbeat
for f in ocf-binaries ocf-directories ocf-returncodes ocf-shellfuncs
do
	ln -s ../../lib/heartbeat/$f .$f
done
)
###########################################################

%clean
###########################################################
if
  [ -n "${RPM_BUILD_ROOT}" -a "${RPM_BUILD_ROOT}" != "/" ]
then
  rm -rf $RPM_BUILD_ROOT
fi
rm -rf $RPM_BUILD_DIR/resource-agents
###########################################################
%if 0%{?suse_version}
%preun -n ldirectord
%stop_on_removal ldirectord
%postun -n ldirectord
%insserv_cleanup
%endif

%if 0%{?fedora}
%preun -n ldirectord
/sbin/chkconfig --del ldirectord
%postun -n ldirectord -p /sbin/ldconfig
%post -n ldirectord
/sbin/chkconfig --add ldirectord
%endif

%files
###########################################################
%defattr(-,root,root)
%dir /usr/lib/ocf
%dir /usr/lib/ocf/resource.d
%dir /usr/lib/ocf/lib
%dir %{_datadir}/%{name}
%dir %{_datadir}/%{name}/ocft
%{_datadir}/%{name}/ocft/configs
%{_datadir}/%{name}/ocft/caselib
%{_datadir}/%{name}/ocft/README
%{_datadir}/%{name}/ocft/README.zh_CN
/usr/lib/ocf/resource.d/heartbeat
/usr/lib/ocf/lib/heartbeat
%{_sbindir}/ocf-tester
%{_sbindir}/ocft
%{_sbindir}/sfex_init
%{_sbindir}/sfex_stat
%{_includedir}/heartbeat
%dir %attr (1755, root, root)	%{_var}/run/resource-agents

%doc AUTHORS
%doc COPYING
%doc COPYING.GPLv3
%doc ChangeLog
%doc %{_datadir}/%{name}/ra-api-1.dtd
%doc %{_mandir}/man7/*.7*
%doc %{_mandir}/man8/ocf-tester.8*
%doc doc/README.webapps

# For compatability with pre-existing agents
%dir /etc/ha.d
/etc/ha.d/shellfuncs

%{_libdir}/heartbeat/send_arp
%{_libdir}/heartbeat/sfex_daemon
%{_libdir}/heartbeat/findif
%{_libdir}/heartbeat/tickle_tcp

%files -n ldirectord
###########################################################
%defattr(-,root,root)
%doc ldirectord/ldirectord.cf
%doc %{_mandir}/man8/ldirectord.8*
%dir /etc/ha.d/resource.d
#%doc %{_mandir}/man8/supervise-ldirectord-config.8*
%{_sbindir}/ldirectord
/sbin/rcldirectord
#%{_sbindir}/supervise-ldirectord-config
%{_sysconfdir}/init.d/ldirectord
%{_sysconfdir}/ha.d/resource.d/ldirectord
%config(noreplace) %{_sysconfdir}/logrotate.d/ldirectord

%changelog

