cd ../../../../libmedia/android/jni/; ndk-build; cd -
cd  ../../android/jni/; ndk-build; cd -
ant debug install; adb logcat -c; adb logcat *:V | grep -E 'zylthinking|DEBUG'
