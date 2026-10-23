Name:       harbour-yalelock

Summary:    Yale Lock
Version:    0.1.0
Release:    1
License:    MIT
URL:        http://example.org/
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(sailfishsecrets)
BuildRequires:  pkgconfig(openssl)
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Sql)
BuildRequires:  pkgconfig(Qt5DBus)
# No pkgconfig(Qt5Bluetooth): Sailfish OS's Qt5 has no QtConnectivity. The
# lock is reached through BlueZ's D-Bus API with Qt5DBus instead (see
# src/ble/).
# No pkgconfig(Qt5LinguistTools) virtual exists on Sailfish OS - CMake's
# find_package(Qt5 COMPONENTS LinguistTools) locates it via the CMake
# package config instead, which comes from this plain package.
BuildRequires:  qt5-qttools-linguist
BuildRequires:  desktop-file-utils
BuildRequires:  cmake

%description
Native control for a Yale Access / August smart lock over Bluetooth LE - lock,
unlock and read the door/lock status directly from the phone, with no cloud
account and no Internet permission. The lock's offline handshake key is
entered once (see README.md) and stored in the Sailfish Secrets vault, never
in plain text on disk.

%prep
%setup -q -n %{name}-%{version}

%build

%cmake

%make_build


%install
%make_install


desktop-file-install --delete-original         --dir %{buildroot}%{_datadir}/applications                %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
