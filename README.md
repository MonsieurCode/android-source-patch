# android-source-patch

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

Install required packages - ubuntu 13.10 and 14.04

    sudo apt-get -y install git-core gnupg flex bison gperf libsdl1.2-dev\
    libesd0-dev libwxgtk2.8-dev squashfs-tools build-essential zip curl\
    libncurses5-dev zlib1g-dev pngcrush schedtool libxml2 libxml2-utils\
    xsltproc lzop libc6-dev schedtool g++-multilib lib32z1-dev lib32ncurses5-dev\
    lib32readline-gplv2-dev gcc-multilib libswitch-perl
   
Install ARM toolchain and building kernel related pacakges

    sudo apt-get install lzop libncurses5-dev
   
Install libraries for other tools

    sudo apt-get install libssl1.0.0 libssl-dev u-boot-tools uuid-dev liblzo2-dev libswitch-perl
   
## Install ADB

    sudo apt-get install android-tools-adb

## Downloading the Source

The Android source code is located in an [archive](http://udoo.org/download/files/Sources/) hosted by Udoo.

    cd ~/Downloads
    wget http://download.udoo.org/files/Sources/UDOO_Android_4.3_Source_v2.0.tar.gz
    wget http://udoo.org/download/files/Sources/UDOO_Android_4.4.2_Source_v1.0.tar.gz
    mkdir ~/Documents/android-source
    
Dowload the patch

    git clone git@github.com:MonsieurCode/android-source-patch.git

Apply the patch

    rsync -a -P android-source-patch/ android-source

## Develop Driver (If Necessary)

Here is a comprehensive guide to [HID driver development](http://lii-enac.fr/en/architecture/linux-input/multitouch-howto.html).

## Modify Kernel [HID](http://lii-enac.fr/en/architecture/linux-input/multitouch-devices.html) Source to Enable Touch

[Add Touchscreen Drivers](http://www.chalk-elec.com/?p=2028)

    static const struct hid_device_id mt_devices[] = {
        
        /* AUO 10" */
        { .driver_data = MT_CLS_DEFAULT, HID_USB_DEVICE(0x27C6,0x07D3) }, 
        
        /* Chalkboard Electronics 7" and 10" */
        { .driver_data = MT_CLS_DEFAULT, HID_USB_DEVICE(0x04D8,0xF724) },
        
        /* Chalkboard Electronics 14" */
        { .driver_data = MT_CLS_DEFAULT, HID_USB_DEVICE(0x0EEF,0xA107) },
        
        // here the rest of definitions comes
        
        Benedikt Dietrich

## Modify Kernel Video Source to Achieve 720p

Place the generic 720p entry in kernel/drivers/video/omap2/displays/panel-generic-dpi.c with the following...

    {
       {
           .x_res = 1280,
           .y_res = 800,
           ...
        },
        .name = “generic”,
    },
        
## Configure Kernel (This is very important!)

    . setup udoo-eng
    cd android-source/udoo/4.4.2/kernel_imx
    make menuconfig

Navigate to Device Drivers > HID > Special Drivers. **Make sure HID Multitouch panels are included which corresponds to device.internal = 1 in your Input Device Configuration (IDC).** Alternatively, a modularized feature would correspond to device.internal = 0. IDCs will be explained in detail later. 
    
![Kernel Configuration](https://slack-files.com/files-tmb/T02FPMKLD-F0EH0P1UH-7b67cf73f9/kernel_configuration_special_hid_drivers_720.png)

## Build U-Boot

    . setup udoo-eng
    cd android-source/udoo/4.4.2/bootable/bootloader/uboot-imx
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

1366×768 sets resolution, “M” indicates the kernel will calculate a VESA mode on-the-fly instead of using modedb look-up, “@60” is the # of frames per second.

    setenv bootargs console=ttymxc1,115200 init=/init video=mxcfb0:dev=ldb,1366x768M@60,if=RGB24,bpp=32 video=mxcfb1:off video=mxcfb2:off fbmem=28M vmalloc=400M androidboot.console=ttymxc1 androidboot.hardware=freescale mem=1024M
    saveenv

## Push System Files to Android Filesystem

**All built-in touch devices should have input device configuration files.** 

Touch screens are touch devices that are associated with a display such that the user has the impression of directly manipulating items on screen.

It is important to ensure that the value of the device.internal property is set correctly for all internal input devices.

    touch.deviceType = touchScreen

This property specifies whether the input device is an internal built-in component as opposed to an externally attached (most likely removable) peripheral.

If the value is 0, the device is external.
If the value is 1, the device is internal.
If the value is not specified, the default value is 0 for all devices on the USB (BUS_USB) or Bluetooth (BUS_BLUETOOTH) bus, 1 otherwise.

**We set our screen to be included/internal in the kernel configuration step.**

    device.internal = 1

Here is the complete IDC.

    touch.deviceType = touchScreen
    touch.orientationAware = 1
    device.internal = 1
    keyboard.layout = qwerty
    keyboard.characterMap = qwerty2
    keyboard.orientationAware = 1
    keyboard.builtIn = 1
    cursor.mode = navigation
    cursor.orientationAware = 1

You'll know this step has succeed when the Monsieur logo shows on reboot.

    adb remount
    adb push udoo-kitkat-diff/system /system
    adb reboot

![Monsieur Logo on Boot](https://slack-files.com/files-tmb/T02FPMKLD-F06RGJJ6L-b4bb3dab55/boot_1024.png)

## Boot Udoo on 10" 1280x800 HDMI Screen

    setenv bootargs console=ttymxc1,115200 init=/init video=mxcfb0:dev=hdmi,1280x800M@60,if=RGB24,bpp=32 video=mxcfb1:off video=mxcfb2:off fbmem=28M vmalloc=400M androidboot.console=ttymxc1 androidboot.hardware=freescale mem=1024M
    saveenv
    
## Fix Resolution

You may run into a 640 x 480 resolution. Changing the kernel resolution and setting the environment boot arguments may not be not enough. There is a problem in reading the EDID code from HDMI monitor. Follow this [guide](http://www.udoo.org/docs/Troubleshooting/How_Can_I_Solve_My_HDMI_Issues) to resolve the issue. Namely, you need to find a valid /etc/edid.txt file and copy it to the filesystem.

Interestingly, you can override the screen's size and density.

    adb shell wm size 1280x800
    adb shell wm size reset
    adb shell wm density 160
    adb shell wm reset
    
Unfortunately, this doesn't address the physical size issue. That's a kernel issue.

## Congratulations

If all steps were followed correctly, you should have successfully built an Udoo Android Kitkat system image with multi-touch capabilities.

## Known Issues
If you attempt to push the complete source code to this repository, you will get the following error.

    compressing objects: 100% (3963/3963), done.
    error: pack-objects died of signal 13
    error: failed to push some refs to 'git@github.com:MonsieurCode/udoo-source.git' 

## Helpful Links 

1. [UDOO compile Android 4.2.2 from sources](http://elinux.org/UDOO_compile_Android_4.2.2_from_sources)
2. [Build Android for Radxa Rock Pro](http://radxa.com/Rock/Android_Build)
3. [Android Open Source Project](https://source.android.com/source/requirements.html)
4. [How To Install Java on Ubuntu with Apt-Get](https://www.digitalocean.com/community/tutorials/how-to-install-java-on-ubuntu-with-apt-get)
5. [UDOO Setup LVDS Panels](http://elinux.org/UDOO_setup_lvds_panels)
6. [Input Device Configuration Files](https://source.android.com/devices/input/input-device-configuration-files.html)
7. [Touch Devices | Android Open Source Project](https://source.android.com/devices/input/touch-devices.html)
8. [How to setup correct LCD resolution](http://www.chalk-elec.com/?p=1420)
9. [How to use our new 10-inch integrated LCD](http://www.chalk-elec.com/?p=2060)
10. [Multitouch Compatibility Table](http://lii-enac.fr/en/architecture/linux-input/multitouch-devices.html)
