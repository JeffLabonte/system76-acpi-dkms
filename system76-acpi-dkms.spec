#
# spec file for package system76-acpi-dkms
#
# Copyright (c) 2021 SUSE LLC
#
# All modifications and additions to the file contributed by third parties
# remain the property of their copyright owners, unless otherwise agreed
# upon. The license for this file, and modifications and additions to the
# file, is the same license as for the pristine package itself (unless the
# license for the pristine package is not an Open Source License, in which
# case the license is the MIT License). An "Open Source License" is a
# license that conforms to the Open Source Definition (Version 1.9)
# published by the Open Source Initiative.

# Please submit bugfixes or comments via https://bugs.opensuse.org/
#


Name:           system76-acpi-dkms
Version:        1.0.2
Release:        0
Summary:        This provides the system76_acpi in-tree driver for systems missing it.
License:        GPL-2.0
URL:            https://github.com/pop-os/system76-acpi-dkms
Requires:       dkms
Requires:       kernel-devel

%description
System76 ACPI DKMS (Dynamic Kernel Module Support)
---
This provides the system76_acpi in-tree driver for systems missing it.

%prep
{{{ git_dir_setup_macro }}}

%install
install -D -m 0644 *.c -t "%{buildroot}%{_usrsrc}/system76-acpi-{{{ git_dir_versio
    n }}}/"
install -m 0644 Makefile -t "%{buildroot}%{_usrsrc}/system76-acpi-{{{ git_dir_vers    ion }}}/"
install -m 0644 debian/system76-acpi-dkms.dkms "%{buildroot}%{_usrsrc}/system76-ac    pi-{{{ git_dir_version }}}/dkms.conf"make_install
find %{buildroot} -type f -name "*.la" -delete -print

%post
sed -i 's/PACKAGE_VERSION="#MODULE_VERSION#"/PACKAGE_VERSION="{{{ git_dir_version     }}}"/g' %{_usrsrc}/system76-acpi-{{{ git_dir_version }}}/dkms.conf
# change module id also in C code.
sed -i 's/MODULE_VERSION("\(.*\)");/MODULE_VERSION("\1-{{{ git_dir_version }}}");/    g' %{_usrsrc}/system76-acpi-{{{ git_dir_version }}}/system76_acpi.c
/usr/bin/env dkms add -m system76-acpi -v {{{ git_dir_version }}} --rpm_safe_upgra    de
/usr/bin/env dkms build -m system76-acpi -v {{{ git_dir_version }}}
/usr/bin/env dkms install -m system76-acpi -v {{{ git_dir_version }}} --force


%postun
rm -rfv /var/lib/dkms/system76-acpi/{{{ git_dir_version }}}

%preun
/usr/bin/env rmmod system76-acpi || :
/usr/bin/env dkms remove -m system76-acpi -v {{{ git_dir_version }}} --all --rpm_s    afe_upgrade || :


%files
${_usrusr}/system76-acpi-{{{ git_dir_version }}}

%changelog
{{{ git_dir_changelog }}}
