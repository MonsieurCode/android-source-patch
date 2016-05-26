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
