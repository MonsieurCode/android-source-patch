adb root
adb shell mkdir /data/altair
adb shell chown radio:radio /data/altair
adb push out/altAtTest /data/altair/
adb shell chown radio:radio /data/altair/altAtTest
adb shell chmod 777 /data/altair/altAtTest
