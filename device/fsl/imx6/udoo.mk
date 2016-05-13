# This is a FSL Android Reference Design platform based on i.MX6Q ARD board
# It will inherit from FSL core product which in turn inherit from Google generic

$(call inherit-product, device/fsl/imx6/imx6.mk)
$(call inherit-product-if-exists,vendor/google/products/gms.mk)
$(shell touch device/fsl/udoo/fstab.freescale)

# Overrides
PRODUCT_NAME := udoo
PRODUCT_DEVICE := udoo

PRODUCT_COPY_FILES += \
	device/fsl/udoo/required_hardware.xml:system/etc/permissions/required_hardware.xml \
	device/fsl/udoo/init.rc:root/init.freescale.rc \
	device/fsl/udoo/gpsreset.sh:system/etc/gpsreset.sh \
	device/fsl/udoo/audio_policy.conf:system/etc/audio_policy.conf \
	device/fsl/udoo/audio_effects.conf:system/vendor/etc/audio_effects.conf \
	device/fsl/udoo/fstab.freescale:root/fstab.freescale \
	device/fsl/udoo/input/sitronix-i2c-touch-mt.idc:system/usr/idc/sitronix-i2c-touch-mt.idc \
	device/fsl/udoo/input/Vendor_0596_Product_0001.idc:system/usr/idc/Vendor_0596_Product_0001.idc

#Superuser installation
PRODUCT_COPY_FILES += \
	device/fsl/udoo/Superuser/arm/su:system/xbin/daemonsu \
	device/fsl/udoo/Superuser/arm/su:system/xbin/su \
	device/fsl/udoo/Superuser/common/install-recovery.sh:system/etc/install-recovery.sh \
	device/fsl/udoo/Superuser/common/99SuperSUDaemon:system/etc/init.d/99SuperSUDaemon

# GPU files

DEVICE_PACKAGE_OVERLAYS := device/fsl/udoo/overlay

PRODUCT_CHARACTERISTICS := tablet

PRODUCT_AAPT_CONFIG += xlarge large tvdpi hdpi

PRODUCT_COPY_FILES += \
	frameworks/native/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
	frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
	frameworks/native/data/etc/android.hardware.wifi.direct.xml:system/etc/permissions/android.hardware.wifi.direct.xml \
	frameworks/native/data/etc/android.hardware.sensor.light.xml:system/etc/permissions/android.hardware.sensor.light.xml \
	frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:system/etc/permissions/android.hardware.sensor.accelerometer.xml \
	frameworks/native/data/etc/android.hardware.faketouch.xml:system/etc/permissions/android.hardware.faketouch.xml \
	frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
	frameworks/native/data/etc/android.software.sip.voip.xml:system/etc/permissions/android.software.sip.voip.xml \
	frameworks/native/data/etc/android.hardware.usb.host.xml:system/etc/permissions/android.hardware.usb.host.xml \
	frameworks/native/data/etc/android.hardware.usb.accessory.xml:system/etc/permissions/android.hardware.usb.accessory.xml \
	frameworks/native/data/etc/android.hardware.bluetooth_le.xml:system/etc/permissions/android.hardware.bluetooth_le.xml
#	frameworks/native/data/etc/android.hardware.usb.accessory.xml:system/etc/permissions/android.hardware.usb.accessory.xml

# for PDK build, include only when the dir exists
# too early to use $(TARGET_BUILD_PDK)
ifneq ($(wildcard packages/wallpapers/LivePicker),)
PRODUCT_COPY_FILES += \
	packages/wallpapers/LivePicker/android.software.live_wallpaper.xml:system/etc/permissions/android.software.live_wallpaper.xml
endif

PRODUCT_PACKAGES += 	\
	rfkill 		\
	iwconfig 	\
	iwlist 		\
	BluetoothLeGatt \
	Camera2 	\
	setotg 		\
	lsusb		\
	SimpleExplorer	\
	adbWireless	\
	Superuser
