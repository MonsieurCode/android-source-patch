# udoo-source-patch

This repository contains any modifications to the original Udoo Android source code. **This repository does NOT include the entire UDOO Android KitKat source code.** You will need to download the [complete source code archive](http://udoo.org/download/files/Sources/), unzip it, and merge the changes here at build time. For generic Android Source building instructions, click [here](http://source.android.com/source/building.html).

## Requirements

The Android build is routinely tested in-house on recent versions of [Ubuntu](http://www.ubuntu.com/download) LTS (14.04), but most distributions should have the required build tools available.

Before you download and build the Android source, ensure your system meets the following requirements:

A Linux or Mac OS system. It is also possible to build Android in a virtual machine on unsupported systems such as Windows. If you are running Linux in a virtual machine, you need at least 16GB of RAM/swap and 100GB or more of disk space in order to build the Android tree. See disk size requirements below.
A 64-bit environment is required for Gingerbread (2.3.x) and newer versions, including the master branch. You can compile older versions on 32-bit systems.
At least 100GB of free disk space for a checkout, 150GB for a single build, and 200GB or more for multiple builds. If you employ ccache, you will need even more space.
Python 2.6 -- 2.7, which you can download from python.org.
GNU Make 3.81 -- 3.82, which you can download from gnu.org,
JDK 7 to build the master branch of Android in the Android Open Source Project (AOSP); JDK 6 to build Gingerbread through KitKat; JDK 5 for Cupcake through Froyo. See Initializing a Build Environment for installation instructions by operating system.
Git 1.7 or newer. You can find it at git-scm.com.

## Download and Install Ubuntu

[Ubuntu 14.04](http://www.ubuntu.com/download)

## Install Tweak Tools

    sudo apt-get install unity-tweak-tool gnome-tweak-tool

## Install Guake

    sudo apt-get install guake
    sudo ln -s /usr/share/applications/guake.desktop /etc/xdg/autostart/

## Install Java 6

    sudo add-apt-repository ppa:webupd8team/java
    sudo apt-get update
    sudo apt-get install oracle-java6-installer

## Select Oracle Java 6 as the Primary Alternative 

    sudo update-alternatives --config java
    sudo update-alternatives --config javac

## Install Required Packages

    sudo apt-get install git gnupg flex bison gperf build-essential \
    zip curl libc6-dev libncurses5-dev:i386 x11proto-core-dev \
    libx11-dev:i386 libreadline6-dev:i386 libglapi-mesa:i386 libgl1-mesa-glx:i386 \
    libgl1-mesa-dev g++-multilib mingw32 tofrodos \
    python-markdown libxml2-utils xsltproc zlib1g-dev:i386 \
    lib32z1 lib32ncurses5 lib32bz2-1.0 u-boot-tools minicom libncurses5-dev \
    liblzo2-dev:i386 libswitch-perl libtool

Download and install [libuuid](http://sourceforge.net/projects/libuuid/?source=typ_redirect)

    ./configure && make && sudo make install
    
Download and install [zlib](https://launchpad.net/ubuntu/+source/zlib/1:1.2.8.dfsg-1ubuntu1)

    ./configure && make && sudo make install

## Downloading the Source

The Android source code is located in an [archive](http://udoo.org/download/files/Sources/) hosted by Udoo.

    cd ~/Downloads
    wget http://udoo.org/download/files/Sources/UDOO_Android_4.4.2_Source_v1.0.tar.gz
    
## Modify Kernel

[Add Touchscreen Drivers](http://www.chalk-elec.com/?p=2028)

    static const struct hid_device_id mt_devices[] = {
        
        /* AUO 10" */
        { .driver_data = MT_CLS_DEFAULT, HID_USB_DEVICE(0x27C6,0x07D3) }, 
        
        /* Chalkboard Electronics 7" and 10" */
        { .driver_data = MT_CLS_DEFAULT, HID_USB_DEVICE(0x04D8,0xF724) },
        
        /* Chalkboard Electronics 14" */
        { .driver_data = MT_CLS_DEFAULT, HID_USB_DEVICE(0x0EEF,0xA107) },
        
        // here the rest of definitions comes
        

## Configure Kernel (This is very important!)

    . setup udoo-eng
    cd udoo-source/kernel_imx
    make menuconfig

Device Drivers > HID > Special Drivers > HID Multitouch panels (*)
    
![Kernel Configuration](https://slack-files.com/files-tmb/T02FPMKLD-F0EH0P1UH-7b67cf73f9/kernel_configuration_special_hid_drivers_720.png)

## Build UBoot

    . setup udoo-eng
    cd udoo-kitkat/bootable/bootloader/uboot-imx
    ./compile.sh -c
    ./compile.sh

## Build Android System Image

Build everything with make. GNU make can handle parallel tasks with a -jN argument, and it's common to use a number of tasks N that's between 1 and 2 times the number of hardware threads on the computer being used for the build. For example, on a dual-E5520 machine (2 CPUs, 4 cores per CPU, 2 threads per core), the fastest builds are made with commands between make -j16 and make -j32.

    . setup udoo-eng
    cd udoo-kitkat
    time make -j2

## Flash SD Card

    croot
    sudo -E ./make_sd.sh /dev/sdc

Connect a serial cable to the Udoo and your computer. [Establish a connection](http://www.udoo.org/tutorial/connecting-via-serial-cable/).

A video tutorial of this step can be found [here](https://www.youtube.com/watch?v=7CYsKJ1kqsk).

Configure Minicom

    sudo minicom -sw

Run Minicom

    sudo minicom -w

## Boot Udoo on 15" Screen

    setenv bootargs console=ttymxc1,115200 init=/init video=mxcfb0:dev=ldb,1366x768M@60,if=RGB24,bpp=32 video=mxcfb1:off video=mxcfb2:off fbmem=28M vmalloc=400M androidboot.console=ttymxc1 androidboot.hardware=freescale mem=1024M
    saveenv

## Push System Files to Android Filesystem

You'll know this step has succeed when the Monsieur logo shows on reboot.

    adb remount
    adb push udoo-kitkat-diff/system /system
    adb reboot

![Monsieur Logo on Boot](https://slack-files.com/files-tmb/T02FPMKLD-F06RGJJ6L-b4bb3dab55/boot_1024.png)

## Boot Udoo on 10" Screen

    setenv bootargs console=ttymxc1,115200 init=/init video=mxcfb0:dev=hdmi,1280x800M@60,if=RGB24,bpp=32 video=mxcfb1:off video=mxcfb2:off fbmem=28M vmalloc=400M androidboot.console=ttymxc1 androidboot.hardware=freescale mem=1024M
    saveenv

## Congratulations

If all steps were followed correctly, you should have successfully built an Udoo Android Kitkat system image with multi-touch capabilities.

## Known Issues
If you attempt to push the complete source code to this repository, you will get the following error.

    compressing objects: 100% (3963/3963), done.
    error: pack-objects died of signal 13
    error: failed to push some refs to 'git@github.com:MonsieurCode/udoo-source.git' 

## Bonus: Setup a Mac OS build environment

You can create a case-sensitive filesystem within your existing Mac OS environment using a disk image. To create the image, launch Disk Utility and select "New Image". A size of 25GB is the minimum to complete the build; larger numbers are more future-proof. Using sparse images saves space while allowing to grow later as the need arises. Be sure to select "case sensitive, journaled" as the volume format.

You can also create it from a shell with the following command:

    hdiutil create -type SPARSE -fs 'Case-sensitive Journaled HFS+' -size 40g ~/android.dmg

Go to https://github.com/phracker/MacOSX-SDKs, get MacOSX10.10.sdk and MacOSX10.9.sdk folders and copy them into

    /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs
    
Setup [JENV](http://www.jenv.be/) to use multiple Java Versions

    brew install jenv
    
Additional help should be found [here](http://tryge.com/2013/06/15/build-android-from-source-macosx/).

## References 

1. [UDOO compile Android 4.2.2 from sources](http://elinux.org/UDOO_compile_Android_4.2.2_from_sources)
2. [Android Open Source Project](https://source.android.com/source/requirements.html)
3. [How To Install Java on Ubuntu with Apt-Get](https://www.digitalocean.com/community/tutorials/how-to-install-java-on-ubuntu-with-apt-get)
4. [UDOO Setup LVDS Panels](http://elinux.org/UDOO_setup_lvds_panels)
5. [Input Device Configuration Files](https://source.android.com/devices/input/input-device-configuration-files.html)
5. [Touch Devices | Android Open Source Project](https://source.android.com/devices/input/touch-devices.html)
