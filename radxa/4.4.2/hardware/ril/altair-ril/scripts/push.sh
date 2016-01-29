adb root
adb push ../../../out/target/product/x86/system/lib/libaltair-ril.so /system/lib/libaltair-ril.so
adb shell setprop rild.libargs "-d /dev/ttyACM0"
adb shell setprop rild.libpath "/system/lib/libaltair-ril.so"
adb shell pkill rild
