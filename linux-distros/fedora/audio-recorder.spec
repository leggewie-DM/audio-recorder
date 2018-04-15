%define releasenum 2

Name: audio-recorder
Version: 2.0
Release: %{releasenum}%{?dist} 
Summary: Recording applet for your GNOME panel.

License: GPL2
URL: https://launchpad.net/audio-recorder
Source0: %{name}-%{version}-%{releasenum}.tar.gz
#Patch0: audio-recorder-configure.patch
#Patch1: audio-recorder-port-to-gtk3.patch
#Patch2: audio-recorder-include.patch

BuildRequires: gtk+-devel gtk3-devel glib-devel gstreamer1-devel gstreamer1-plugins-base-devel intltool pulseaudio-libs-devel dbus-devel cairo-gobject libappindicator-gtk3-devel

%description
This amazing program allows you to record your favourite music or audio to a file.
It can record audio from your system's soundcard, microphones, browsers, webcams & more.
Put simply; if it plays out of your loudspeakers you can record it.

It has an advanced timer that can:
* Start, stop or pause recording at a given clock time.
* Start, stop or pause after a time period.
* Stop when the recorded file size exceeds a limit.
* Start recording on voice or sound (user can set the audio level).
* Stop or pause recording on "silence" (user can set the audio level and delay).

The recording can be atomatically controlled by:
* RhytmBox and Banshee audio player.
* Audacious, Amarok, VLC and other MPRIS compatible players.
* Skype. It can automatically record all your Skype calls without any user interaction.

This program supports several audio (output) formats such as OGG audio, Flac, MP3 and WAV.

User can also control the recorder from command line with --command <arg> option.
See audio-recorder --help for more information.

%prep
%setup -q -n audio-recorder
#%patch0
#%patch1
#%patch2

%build

aclocal && autoconf && automake -a

%configure
make %{?_smp_mflags}
#make CPPFLAGS=-I/usr/include/libgnome-media-profiles-3.0/libgnome-media-profiles/ %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

%postun
if [ $1 -eq 0 ] ; then
    /usr/bin/glib-compile-schemas %{_datadir}/glib-2.0/schemas &> /dev/null || :
fi

%posttrans
    /usr/bin/glib-compile-schemas %{_datadir}/glib-2.0/schemas &> /dev/null || :

%files
%defattr(-,root,root,-)
%doc COPYING README
%{_bindir}/audio-recorder
%{_datadir}/applications/audio-recorder.desktop
%{_datadir}/audio-recorder/*
%{_datadir}/icons/hicolor/*
%{_datadir}/locale/*
%{_datadir}/pixmaps/*
%{_datadir}/glib-2.0/schemas/org.gnome.audio-recorder.gschema.xml

%changelog
* Fri Aug 29 2014 Ron. H <no email> - 1.4
- Updated readme.txt and audio-recorder.spec.

* Tue Dec 10 2013 Ronald Luther Humble <no email> - 1.4
- Version 1.4.
- Added -1 to Source0: %{name}-%{version}-1.tar.gz

* Thu Aug 15 2013 Francesco Frassinelli  <no email> - 1.2
- Fixed broken line and removed LF with dos2unix command.

* Mon Jul 22 2013 Ronald (Royboy626) <no email> - 1.1.2
- added dependencies, ordered changelog and changed Version number.

* Wed May 08 2013 Osmo Antero <osmoma@online.no> - 1.1
- removed dependency to dbus-glib-devel. Project now needs GDBus only.

* Sat Oct 20 2012 Osmo Antero <osmoma@online.no> - 0.9-6
- porting to Fedora 18.
- notice: I have not generated packages for Fedora 18, but I changed the
- BuildRequires string. Requires now Gstreamer 1.0 and cairo-gobject.

* Mon Apr 2 2012 Paolo Patruno <pat1@localhost.localdomain> - 0.5-2
- updated to 0.8.1

* Sun Mar 18 2012 Paolo Patruno <p.patruno@iperbole.bologna.it> - 0.5-2
- patch makes audio-recorder compile and run with gtk3 by Antonio Ospite

* Sat Feb 11 2012 Paolo Patruno <p.patruno@iperbole.bologna.it> - 0.5-1
- starting with F16 release

