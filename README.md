# udoo-source

This repository contains any modifications to the original Udoo Android source code. 
You will need to download the [complete source code archive](http://udoo.org/download/files/Sources/), unzip it, and merge the changes here at build time. For generic Android Source building instructions, click [here](http://source.android.com/source/building.html).

## Compilation Guide

[Download Ubuntu](http://www.ubuntu.com/download)

Install Required Packages

    sudo apt-get install git gnupg flex bison gperf build-essential \
    zip curl libc6-dev libncurses5-dev:i386 x11proto-core-dev \
    libx11-dev:i386 libreadline6-dev:i386 libgl1-mesa-glx:i386 \
    libgl1-mesa-dev g++-multilib mingw32 tofrodos \
    python-markdown libxml2-utils xsltproc zlib1g-dev:i386 \
    lib32z1 lib32ncurses5 lib32bz2-1.0 u-boot-tools minicom libncurses5-dev \
    uuid-dev:i386    liblzo2-dev:i386


    sudo ln -s /usr/lib/i386-linux-gnu/mesa/libGL.so.1 /usr/lib/i386-linux-gnu/libGL.so

Install Java 6

    sudo apt-get install oracle-java6-installer

[Download Udoo Android Source](http://udoo.org/download/files/Sources/)

[Add Touchscreen Drivers](http://www.chalk-elec.com/?p=2028)

[Build Source](http://elinux.org/UDOO_compile_Android_4.2.2_from_sources)

    android-source/Udoo/4.4.2/out/target/product/udoo/system.img

[Flash SD Card](http://www.tweaking4all.com/hardware/raspberry-pi/macosx-apple-pi-baker/) 

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
