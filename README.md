# udoo-source

This repository contains any modifications to the original Udoo Android source code. 
You will need to download the [complete source code archive](http://udoo.org/download/files/Sources/), unzip it, and merge the changes here at build time. For generic Android Source building instructions, click [here](http://source.android.com/source/building.html).

## Compilation Guide

[Download Ubuntu](http://www.ubuntu.com/download)

Install Java 6

    sudo apt-get install oracle-java6-installer

Install Required Packages

    sudo apt-get install git gnupg flex bison gperf build-essential \
    zip curl libc6-dev libncurses5-dev:i386 x11proto-core-dev \
    libx11-dev:i386 libreadline6-dev:i386 libgl1-mesa-glx:i386 \
    libgl1-mesa-dev g++-multilib mingw32 tofrodos \
    python-markdown libxml2-utils xsltproc zlib1g-dev:i386 \
    lib32z1 lib32ncurses5 lib32bz2-1.0 u-boot-tools minicom libncurses5-dev \
    liblzo2-dev:i386

Download and install [libuuid](http://sourceforge.net/projects/libuuid/?source=typ_redirect)

    ./configure
    make
    make install

[Download Udoo Android Source](http://udoo.org/download/files/Sources/)

[Add Touchscreen Drivers](http://www.chalk-elec.com/?p=2028)

    static const struct hid_device_id mt_devices[] = {
        
        /* Chalkboard Electronics 7" and 10" */
        { .driver_data = MT_CLS_DEFAULT, HID_USB_DEVICE(0x04D8,0xF724) },
        
        /* Chalkboard Electronics 14" */
        { .driver_data = MT_CLS_DEFAULT, HID_USB_DEVICE(0x0EEF,0xA107) },
        
        // here the rest of definitions comes

[Build Source](http://elinux.org/UDOO_compile_Android_4.2.2_from_sources)

    . setup udoo-eng
    cd [udoo-android-dev]/bootable/bootloader/uboot-imx
    ./compile.sh
    make

[Flash SD Card](http://www.tweaking4all.com/hardware/raspberry-pi/macosx-apple-pi-baker/)

    croot
    sudo -E ./make_sd.sh /dev/sdc

[Set Environment Boot Arguments](http://elinux.org/UDOO_setup_lvds_panels)

Connect a serial cable to the Udoo and your computer. [Establish a connection](http://www.udoo.org/tutorial/connecting-via-serial-cable/).

10" Screen

    setenv bootargs console=ttymxc1,115200 init=/init video=mxcfb0:dev=hdmi,1280x800M@60,if=RGB24,bpp=32 video=mxcfb1:off video=mxcfb2:off fbmem=28M vmalloc=400M androidboot.console=ttymxc1 androidboot.hardware=freescale mem=1024M

15" Screen

    setenv bootargs console=ttymxc1,115200 init=/init video=mxcfb0:dev=ldb,1366x768M@60,if=RGB24,bpp=32 video=mxcfb1:off video=mxcfb2:off fbmem=28M vmalloc=400M androidboot.console=ttymxc1 androidboot.hardware=freescale mem=1024M

A video tutorial of this step can be found [here](https://www.youtube.com/watch?v=7CYsKJ1kqsk).

## Known Issues
If you attempt to push the complete source code to this repository, you will get the following error.

    compressing objects: 100% (3963/3963), done.
    error: pack-objects died of signal 13
    error: failed to push some refs to 'git@github.com:MonsieurCode/udoo-source.git' 
