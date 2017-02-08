I. Compiling an audio-recorder rpm package on Fedora or RedHat based distributions.
Ref: https://bugs.launchpad.net/audio-recorder/+bug/1259407

0) Modify audio-recorder.spec.
   Set correct version number.
   Check if there are new dependencies.

1) Move audio-recorder-X.tar.gz (the latest tarball) to ~/rpmbuild/SOURCES/
    It is assumed the rpmbuild environment already exists. It is suggested a user
    reserved for development be used as gcc ccache files in the home
    directory can become numerous and large.

2) `cd ~/rpmbuild/BUILD; tar -xvf ../SOURCES/audio-recorder-X.tar.gz`

3) `cp -iv audio-recorder/linux-distros/fedora/audio-recorder.spec ../SPECS`

4) `rpmbuild -bb ../SPECS/audio-recorder.spec`
     If no errors, the last few lines of output will show rpm packages
     created in ~rpmbuild/RPMS subdirectories, which may then be installed.

     As user root, change to the directory containing the rpm. Then
     `rpm -ivh audio-recorder-X.fc21.x86_64.rpm`
     OR `rpm -Uvh` if updating a previous audio-recorder rpm package.


II. Compiling audio-recorder from the development branch

A) Move to a writable directory of your choice and download the code:

     cd /tmp/; bzr branch lp:audio-recorder

   This will create a subdirectory "audio-recorder"

B) Examine the spec file audio-recorder/linux-distros/fedora/audio-recorder.spec
    to determine the name of the tar file you will create in Step "C".

%define releasenum 4

Name: audio-recorder
Version: 1.5
Release: %{releasenum}%{?dist} 
Summary: Recording applet for your GNOME panel.

License: GPL2
URL: https://launchpad.net/audio-recorder
Source0: %{name}-%{version}-%{releasenum}.tar.gz

    In the above snippet from audio-recorder.spec, Source0 indicates (after
     subbing for rpm variables), that the name is "audio-recorder", the
     version is "1.5", and the releasenum is "4". The "dist" variable in the
     "Release:" line would translate to ".fc21" for Fedora 21.

    So the name of the tar file we wish to create for the example above is
     audio-recorder-1.5-4.tar.gz

C) Create the tar file. Assuming /tmp/audio-recorder is our code location:

     cd /tmp; tar -czf audio-recorder-1.5-4.tar.gz audio-recorder

    This will create /tmp/audio-recorder-1.5-4.tar.gz

D) You should now be able to continue with steps 1 through 4 above to compile a
     rpm package.

Note: If have created a rpm package file with an identical name to the rpm
     already installed, you will need to use "rpm -Uvh --force" to install.
     Preferred would be to locally increase the "releasenum" in your spec
     file (Example: %define releasenum 5) prior to rpm creation.
     
    
