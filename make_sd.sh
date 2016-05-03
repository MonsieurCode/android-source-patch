#!/bin/bash

# partition size in MB
BOOTLOAD_RESERVE=8
BOOT_ROM_SIZE=8
SYSTEM_ROM_SIZE=512
CACHE_SIZE=512
RECOVERY_ROM_SIZE=8
VENDER_SIZE=8
MISC_SIZE=8

help() {

bn=`basename $0`
cat << EOF
usage $bn <option> device_node	

where
  device_node			/dev/sdX   or   /dev/mmcblkX

options:
  -h				displays this help message
  -s				only get partition size
  -np 				not partition
  -nw 				not write android image
EOF

}

# check the if root?
userid=`id -u`
if [ $userid -ne "0" ]; then
	echo "You're not root?"
	exit
fi

if [ -z "$OUT" ]; then
    echo "No OUT export variable found! Setup not called in advance..."
    exit 1
fi

# parse command line
moreoptions=1
node="na"
part="na"
cal_only=0
flash_images=1
not_partition=0
not_format_fs=0
while [ "$moreoptions" = 1 -a $# -gt 0 ]; do
	case $1 in
	    -h) help; exit ;;
	    -s) cal_only=1 ;;
	    -nw) flash_images=0 ;;
	    -np) not_partition=1 ;;
	    -nf) not_format_fs=1 ;;
	    *)  moreoptions=0; node=$1 ;;
	esac
	[ "$moreoptions" = 0 ] && [ $# -gt 1 ] && help && exit
	[ "$moreoptions" = 1 ] && shift
done

if [ ! -e ${node} ]; then
	help
	exit
fi


if [[ $node == /dev/sd* ]]; then
	part=${node}
else
	part=${node}p
fi

echo "Trying to unmount partitions"
umount ${part}* > /dev/null 2> /dev/null
sleep 1

# call sfdisk to create partition table
# get total card size
seprate=40
total_size=`sfdisk -s ${node}`
total_size=`expr ${total_size} / 1024`
boot_rom_sizeb=`expr ${BOOT_ROM_SIZE} + ${BOOTLOAD_RESERVE}`
extend_size=`expr ${SYSTEM_ROM_SIZE} + ${CACHE_SIZE} + ${VENDER_SIZE} + ${MISC_SIZE} + ${seprate}`
data_size=`expr ${total_size} - ${boot_rom_sizeb} - ${RECOVERY_ROM_SIZE} - ${extend_size} - ${seprate}`

# create partitions
if [ "${cal_only}" -eq "1" ]; then
cat << EOF
BOOT   : ${boot_rom_sizeb}MB
RECOVERY: ${RECOVERY_ROM_SIZE}MB
SYSTEM : ${SYSTEM_ROM_SIZE}MB
CACHE  : ${CACHE_SIZE}MB
DATA   : ${data_size}MB
MISC   : ${MISC_SIZE}MB
EOF
exit
fi

function format_android
{
    echo "Formatting partitions..."
    mkfs.ext4 -F ${part}4 -Ldata
    mkfs.ext4 -F ${part}5 -Lsystem
    mkfs.ext4 -F ${part}6 -Lcache
    mkfs.ext4 -F ${part}7 -Lvender
}

function flash_android
{
if [ "${flash_images}" -eq "1" ]; then
    echo "Flashing android images..."
    dd if=./bootable/bootloader/uboot-imx/u-boot.bin of=${node} bs=1024 skip=1 seek=1 conv=fsync
    dd if=/dev/zero of=${node} bs=512 seek=1536 count=16 conv=fsync
    dd if=$OUT/boot.img of=${part}1 bs=8192 conv=fsync
    dd if=$OUT/recovery.img of=${part}2 bs=8192 conv=fsync
    dd if=$OUT/system.img of=${part}5 bs=8192 conv=fsync
    # Do it again to be sure it will boot.
    dd if=./bootable/bootloader/uboot-imx/u-boot.bin of=${node} bs=1024 skip=1 seek=1 conv=fsync
fi
}

if [[ "${not_partition}" -eq "1" && "${flash_images}" -eq "1" ]] ; then
    flash_android
    exit
fi


# destroy the partition table
dd if=/dev/zero of=${node} bs=1024 count=1

sleep 3

sfdisk -f -uM ${node} << EOF
,${boot_rom_sizeb},83
,${RECOVERY_ROM_SIZE},83
,${extend_size},5
,${data_size},83
,${SYSTEM_ROM_SIZE},83
,${CACHE_SIZE},83
,${VENDER_SIZE},83
,${MISC_SIZE},83
EOF

sleep 3

# adjust the partition reserve for bootloader.
# if you don't put the uboot on same device, you can remove the BOOTLOADER_RESERVE
# to have 8M space.
# the minimal sylinder for some card is 4M, maybe some was 8M
# just 8M for some big eMMC 's sylinder
sfdisk -f -uM ${node} -N1 << EOF
${BOOTLOAD_RESERVE},${BOOT_ROM_SIZE},83
EOF

sleep 3

sfdisk -V -l -uM ${node}

sleep 1

format_android
flash_android


# For MFGTool Notes:
# MFGTool use mksdcard-android.tar store this script
# if you want change it.
# do following:
#   tar xf mksdcard-android.sh.tar
#   vi mksdcard-android.sh 
#   [ edit want you want to change ]
#   rm mksdcard-android.sh.tar; tar cf mksdcard-android.sh.tar mksdcard-android.sh
