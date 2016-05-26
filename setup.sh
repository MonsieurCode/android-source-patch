wget https://drive.google.com/open?id=0B9oMfppL-n1zR1FrZkRMak90a1U -O ~/Documents/udoo-quad-kitkat.tar.gz
tar zxvf ~/Documents/udoo-quad-kitkat.tar.gz
mv ~/Documents/4.4.2 ~/Documents/udoo-quad-kitkat
cd ~/Documents/udoo-quad-kit
. setup udoo-eng
make -C kernel_imx imx6_udoo_android_defconfig
make
bootable/bootloader/uboot-imx/compile.sh
sudo -E ./make_sd.sh /dev/sdb
