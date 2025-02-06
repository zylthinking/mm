#!/bin/bash

cpus="armv7 arm64"
xcodepath="/Applications/Xcode.app/Contents/Developer"
dir="`pwd`/zyl/ios"

for cpu in ${cpus}
do
    sys="${xcodepath}/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS8.3.sdk"
    minsdk="-miphoneos-version-min=7.0"
    host="arm-apple-darwin"

    export CC="${xcodepath}/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
    export LDFLAGS="-isysroot ${sys}"
    export CFLAGS="${minsdk} -isysroot ${sys} -arch ${cpu} -O3 -DNDEBUG"
    export CPPFLAGS="${minsdk} -isysroot ${sys} -arch ${cpu} -O3 -DNDEBUG"

    rm -rf "${dir}/${cpu}"
    mkdir -p "${dir}/${cpu}"
    find . | grep "\.o$" | xargs rm; find . | grep "\.lo$" | xargs rm

    ./configure --prefix="${dir}/${cpu}" --disable-doc --disable-extra-programs --enable-float-approx --disable-shared --with-pic --host="${host}" --with-sysroot="${sys}"
    make clean; make && make install
done

xcrun -sdk iphoneos lipo -output ../ios/libopus.a -create ${dir}/arm64/lib/libopus.a ${dir}/armv7/lib/libopus.a


dir="`pwd`/zyl/android"
rm -rf "${dir}"
mkdir -p "${dir}"
find . | grep "\.o$" | xargs rm

export RANLIB="/usr/local/ndk-4.8/bin/arm-linux-androideabi-ranlib"
export AR="/usr/local/ndk-4.8/bin/arm-linux-androideabi-gcc-ar"
export CC="/usr/local/ndk-4.8/bin/arm-linux-androideabi-gcc"
export LDFLAGS="-march=armv7-a -O3 -DNDEBUG"
export CFLAGS="-march=armv7-a -O3 -DNDEBUG -fomit-frame-pointer -finline-functions "
export CPPFLAGS="-march=armv7-a -fomit-frame-pointer -finline-functions "

./configure --prefix="${dir}" --disable-doc --disable-extra-programs --enable-float-approx --disable-shared --with-pic --host="arm-linux" --with-gnu-ld --with-sysroot="/usr/local/ndk-4.8/sysroot/"
make clean; make && make install
mkdir -p ../android
cp  ${dir}/lib/libopus.a ../android/libopus.a
