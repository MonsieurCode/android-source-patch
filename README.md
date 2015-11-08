# udoo-source

This repository contains any modifications to the original Udoo Android source code. 
You will need to download the [complete source code archive](http://udoo.org/download/files/Sources/), unzip it, and merge the changes here at build time.

## Compilation Guide

1) [Download Ubuntu](http://www.ubuntu.com/download)

2) Install Java 6

    sudo apt-get install oracle-java6-installer

3) [Download Udoo Android Source](http://udoo.org/download/files/Sources/)

4) [Add Touchscreen Drivers](http://www.chalk-elec.com/?p=2028)

5) [Build Source](http://elinux.org/UDOO_compile_Android_4.2.2_from_sources)

    android-source/Udoo/4.4.2/out/target/product/udoo/system.img

6) [Flash SD Card](http://www.tweaking4all.com/hardware/raspberry-pi/macosx-apple-pi-baker/) 

7) [Set Environment Boot Arguments](http://elinux.org/UDOO_setup_lvds_panels)

Connect a serial cable to the Udoo and your computer. (Establish a connection)[http://www.udoo.org/tutorial/connecting-via-serial-cable/].

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
