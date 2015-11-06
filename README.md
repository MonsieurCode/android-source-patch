# udoo-source

This repository contains any modifications to the original Udoo Android source code. 
You will need to download the [complete source code archive](http://udoo.org/download/files/Sources/), unzip it, and merge the changes here at build time.

## Compilation Guide

1.[Download Ubuntu](http://www.ubuntu.com/download)

2.Install Java 6

    sudo apt-get install oracle-java6-installer

3.[Download Udoo Android Source](http://udoo.org/download/files/Sources/)

4.[Add Touchscreen Drivers](http://www.chalk-elec.com/?p=2028)

5.[Build Source](http://elinux.org/UDOO_compile_Android_4.2.2_from_sources)

    android-source/Udoo/4.4.2/out/target/product/udoo/system.img

6.[Flash SD Card](http://www.tweaking4all.com/hardware/raspberry-pi/macosx-apple-pi-baker/) 

## Known Issues
If you attempt to push the complete source code to this repository, you will get the following error.

    compressing objects: 100% (3963/3963), done.
    error: pack-objects died of signal 13
    error: failed to push some refs to 'git@github.com:MonsieurCode/udoo-source.git' 
