#!/bin/sh

# Customize these parameters according to your environment
ANDROID_NDK="${HOME}/work/hldrd/android-ndk-r9c"

# Check for parameters
if [ ! -d "${ANDROID_NDK}" ]; then
 echo "Android NDK not found in ${ANDROID_NDK}, please edit $0 to fix it."
 exit 1
fi

if [ ! -e "${ANDROID_NDK}/build/tools/make-standalone-toolchain.sh" ]; then
 echo "Your Android NDK is not compatible (make-standalone-toolchain.sh not found)."
 echo "Android NDK r6b is known to work."
 exit 1
fi

# Extract the Android toolchain from NDK
ANDROID_PLATFORM="android-14"
ROOT="`pwd`"
OUT="${ROOT}/out"

if [ ! -d "${OUT}/toolchain" ]; then

${ANDROID_NDK}/build/tools/make-standalone-toolchain.sh \
 --ndk-dir="${ANDROID_NDK}" \
 --platform="${ANDROID_PLATFORM}" \
 --arch=x86 \
 --install-dir="${OUT}/toolchain" \
 || exit 1

fi
# Remove resolv.h because it is quite unusable as is
#rm ${OUT}/toolchain/sysroot/usr/include/resolv.h

export CC="${OUT}/toolchain/bin/i686-linux-android-gcc" 

# Create configure script
cd ${ROOT}

# Compile
make || exit 1

# Done
echo "Build finished"

