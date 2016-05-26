# udoo-quad-kitkat-patch

This repository contains any modifications to the original Udoo Android source code. **This repository does NOT include the entire UDOO Android KitKat source code.** You will need to download the [complete source code archive](http://udoo.org/download/files/Sources/), unzip it, and merge the changes here at build time. For generic Android Source building instructions, click [here](http://source.android.com/source/building.html).

# Download

    wget https://drive.google.com/open?id=0B9oMfppL-n1zR1FrZkRMak90a1U -O ~/Documents/udoo-quad-kitkat.tar.gz
    tar zxvf ~/Documents/udoo-quad-kitkat.tar.gz
    mv ~/Documents/4.4.2 ~/Documents/udoo-quad-kitkat

# Make

    cd ~/Documents/udoo-quad-kit
    . setup udoo-eng
    make -C kernel_imx imx6_udoo_android_defconfig
    make
    bootable/bootloader/uboot-imx/compile.sh
    sudo -E ./make_sd.sh /dev/sdb

# Large Files

    device/lge/hammerhead-kernel/vmlinux.bz2
    external/chromium-trace/trace-viewer/test_data/theverge_trace.json
    external/eclipse-basebuilder/src/eclipse-sourceBuild-srcIncluded-3.6.2.zip
    frameworks/base/media/tests/contents/media_api/videoeditor/H264_BP_640x480_15fps_384kbps_60_0.mp4
    prebuilts/eclipse/deltapack/eclipse-3.7.2-delta-pack.zip
    prebuilts/eclipse/deltapack/eclipse-4.2.2-delta-pack.zip
    prebuilts/eclipse/platform/org.eclipse.platform-3.7.2.zip
    prebuilts/qemu-kernel/x86/vmlinux-vbox

# Download Virtual Box

https://www.virtualbox.org/wiki/Downloads

## Download and Install Ubuntu

[Ubuntu 14.04](http://www.ubuntu.com/download)

## Install Guake (Optional)

    sudo apt-get -y install guake
    sudo ln -s /usr/share/applications/guake.desktop /etc/xdg/autostart/

## Install Java 6

    sudo add-apt-repository ppa:webupd8team/java
    sudo apt-get update
    sudo apt-get -y install oracle-java6-installer

## Select Oracle Java 6 as the Primary Alternative 

    sudo update-alternatives --config java
    sudo update-alternatives --config javac

Install required packages - Ubuntu 14.04

    sudo apt-get install git-core gnupg flex bison gperf libsdl1.2-dev libesd0-dev libwxgtk2.8-dev squashfs-tools build-essential zip curl libncurses5-dev zlib1g-dev pngcrush schedtool libxml2 libxml2-utils xsltproc lzop libc6-dev schedtool g++-multilib lib32z1-dev lib32ncurses5-dev lib32readline-gplv2-dev gcc-multilib libswitch-perl libssl1.0.0 libssl-dev u-boot-tools uuid-dev liblzo2-dev android-tools-adb

As a note, don't add the Android SDK to your path. It will cause your terminal window to crash.

## Downloading the Source

Android source code for [Udoo](http://udoo.org/download/files/Sources/) and [Radxa](http://wiki.radxa.com/Rock2/Android/develop).

    cd ~/Downloads
    wget http://udoo.org/download/files/Sources/UDOO_Android_4.4.2_Source_v1.0.tar.gz
    mkdir ~/Documents/udoo-quad-kitkat
    
Dowload the patch

    git clone git@github.com:MonsieurCode/udoo-quad-kitkat-patch.git

Apply the patch

    rsync -a -P ~/Documents/udoo-quad-kitkat-patch/ ~/Documents/udoo-quad-kitkat/

## Modify Kernel [HID](http://lii-enac.fr/en/architecture/linux-input/multitouch-devices.html) Source to Enable Touch

[Add Touchscreen Drivers](http://www.chalk-elec.com/?p=2028)

    static const struct hid_device_id mt_devices[] = {
        
        /* AUO 10" */
        { .driver_data = MT_CLS_DEFAULT, HID_USB_DEVICE(0x27C6,0x07D3) }, 
        
        /* Chalkboard Electronics 7" */
        { .driver_data = MT_CLS_DEFAULT, HID_USB_DEVICE(0x04D8,0xF724) },
        
        /* Chalkboard Electronics 10" */
        { .driver_data = MT_CLS_DEFAULT, HID_USB_DEVICE(0x1B96,0x07D3) },
        
        /* Chalkboard Electronics 14" */
        { .driver_data = MT_CLS_DEFAULT, HID_USB_DEVICE(0x0EEF,0xA107) },
        
        // ...
        
    }

## Modify Kernel Video Source to Achieve 720p

Add an entry to kernel/drivers/video/mxc_edid.c.

    const struct fb_videomode mxc_cea_mode[64] = {
    /* #48: 1280x800p@60Hz 16:10 */
        [48] = {
                NULL, 60, 1280, 800, 14430, 80, 48, 15, 2, 32, 47, 0,
                FB_VMODE_NONINTERLACED, 0
        },
    }

Replace the 640x480 entry in kernel/drivers/video/mxc_hdmi.c.

    static const struct fb_videomode vga_mode = {
        /* 1280x800 @ 60 Hz, 31.5 kHz hsync */
        NULL, 60, 1280, 800, 14430, 80, 48, 15, 2, 32, 47, 0,
        FB_VMODE_NONINTERLACED, FB_MODE_IS_DETAILED,
    };
        
## Configure Kernel (This is very important!)

    . setup udoo-eng
    cd udoo-quad-kitkat/udoo/4.4.2/kernel_imx
    make menuconfig

Navigate to Device Drivers > HID > Special Drivers. **Make sure HID Multitouch panels are included which corresponds to device.internal = 1 in your Input Device Configuration (IDC).** Alternatively, a modularized feature would correspond to device.internal = 0. IDCs will be explained in detail later. 
    
## Build U-Boot

    . setup udoo-eng
    cd android-source/udoo/4.4.2/bootable/bootloader/uboot-imx
    ./compile.sh -c
    ./compile.sh

## Build Android System Image

Build everything with make. GNU make can handle parallel tasks with a -jN argument, and it's common to use a number of tasks N that's between 1 and 2 times the number of hardware threads on the computer being used for the build. For example, on a dual-E5520 machine (2 CPUs, 4 cores per CPU, 2 threads per core), the fastest builds are made with commands between make -j16 and make -j32.

    . setup udoo-eng
    time make -j2

## Make SD Card

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

## Boot Udoo on 10" 1280x800 HDMI Screen

    setenv bootargs console=ttymxc1,115200 init=/init video=mxcfb0:dev=hdmi,1280x800M@60,if=RGB24,bpp=32 video=mxcfb1:off video=mxcfb2:off fbmem=28M vmalloc=400M androidboot.console=ttymxc1 androidboot.hardware=freescale mem=1024M
    saveenv

## Check Touch

    getevent

## Build Kernel

    make -C kernel_imx imx6_udoo_android_defconfig
    make -C kernel_imx menuconfig
    make bootimage

## Pull Kernel Configuration

Config Kernel

    General setup
        [*] Kernel .config support
            [*] Enable access to .config through /proc/config.gz
            

ADB Shell

    adb remount
    adb pull /proc/config.gz
    
## Make Monsieur Only Launcher

    adb shell mv /system/priv-app/Launcher2.apk /system/priv-app/Launcher2.apk.bak
    
## Make Monsieur System App

    adb shell cp /data/app/co.monsieur.android.machine.apk /system/app/co.monsieur.android.machine.apk

## Congratulations

If all steps were followed correctly, you should have successfully built an Udoo Android Kitkat system image with multi-touch capabilities.

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
11. [The Linux Kernel](https://www.youtube.com/watch?v=XAo1QCQXODo)
