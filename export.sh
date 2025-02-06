
rm -rf ./export/
MEDIA_DIR='./export/media'
mkdir -p $MEDIA_DIR/libmedia

cp ./libmedia/media/fmt.h $MEDIA_DIR/fmt.h
cp ./libmedia/media/fmt_in.h $MEDIA_DIR/fmt_in.h
cp ./libmedia/media/jointor.h $MEDIA_DIR/jointor.h
cp ./libmedia/media/media_buffer.h $MEDIA_DIR/media_buffer.h
cp ./libmedia/media/sortcache.h $MEDIA_DIR/sortcache.h

cp ./libmedia/media/comn/fraction.h $MEDIA_DIR/fraction.h
cp ./libmedia/media/comn/list_head.h $MEDIA_DIR/list_head.h
cp ./libmedia/media/comn/lock.h $MEDIA_DIR/lock.h
cp ./libmedia/media/comn/logmsg.h $MEDIA_DIR/logmsg.h
cp ./libmedia/media/comn/mbuf.h $MEDIA_DIR/mbuf.h
cp ./libmedia/media/comn/mem.h $MEDIA_DIR/mem.h
cp ./libmedia/media/comn/my_buffer.h $MEDIA_DIR/my_buffer.h
cp ./libmedia/media/comn/my_errno.h $MEDIA_DIR/my_errno.h
cp ./libmedia/media/comn/my_handle.h $MEDIA_DIR/my_handle.h
cp ./libmedia/media/comn/mydef.h $MEDIA_DIR/mydef.h
cp ./libmedia/media/comn/now.h $MEDIA_DIR/now.h
cp ./libmedia/media/comn/backtrace.h $MEDIA_DIR/backtrace.h

cp ./libmedia/media/codec/codec.h $MEDIA_DIR/codec.h
cp ./libmedia/media/codec/codec_hook.h $MEDIA_DIR/codec_hook.h

cp ./libmedia/media/platform/area.h $MEDIA_DIR/area.h
cp ./libmedia/media/platform/audioctl.h $MEDIA_DIR/audioctl.h
cp ./libmedia/media/platform/videoctl.h $MEDIA_DIR/videoctl.h
cp ./libmedia/media/platform/ios/glview.h $MEDIA_DIR/glview.h

cp ./libmedia/media/frontend/file_jointor.h $MEDIA_DIR/file_jointor.h
cp ./libmedia/media/frontend/ramfile/ramfile.h $MEDIA_DIR/ramfile.h
cp ./libmedia/media/frontend/capture/audcap.h $MEDIA_DIR/audcap.h
cp ./libmedia/media/frontend/capture/vidcap.h $MEDIA_DIR/vidcap.h
cp ./libmedia/media/muxer/muxer.h $MEDIA_DIR/muxer.h
cp ./libmedia/media/backend/render_jointor.h $MEDIA_DIR/render_jointor.h
cp ./libmedia/media/mmapi/captofile.h $MEDIA_DIR/captofile.h
cp ./libmedia/media/mmapi/playback.h $MEDIA_DIR/playback.h
cp ./libmedia/media/mmapi/mfio.h $MEDIA_DIR/mfio.h
cp ./libmedia/media/mmapi/writer.h $MEDIA_DIR/writer.h

cp ./libmedia/android/src/libmedia/codec.java $MEDIA_DIR/libmedia/codec.java
cp ./libmedia/android/src/libmedia/media.java $MEDIA_DIR/libmedia/media.java
cp ./libmedia/android/src/libmedia/runtime.java $MEDIA_DIR/libmedia/runtime.java
cp ./libmedia/android/src/libmedia/surfacepool.java $MEDIA_DIR/libmedia/surfacepool.java

cp ./libmedia/android/libs/armeabi-v7a/libmedia2.so $MEDIA_DIR/libmedia2.so
cp ./libmedia/android/libs/armeabi-v7a/libomx40.so $MEDIA_DIR/libomx40.so
cp ./libmedia/android/libs/armeabi-v7a/libomx41.so $MEDIA_DIR/libomx41.so
cp ./libmedia/android/libs/armeabi-v7a/libomx50.so $MEDIA_DIR/libomx50.so
cp ./libmedia/android/libs/armeabi-v7a/libruntime.so $MEDIA_DIR/libruntime.so
cp ./libmedia/ios.xcodeproj/bin/libmedia.a $MEDIA_DIR/libmedia.a


MM_DIR='./export/libmm'
mkdir -p $MM_DIR
cp ./projects/nav/nav.h $MM_DIR/nav.h
cp ./projects/nav/mmv.h $MM_DIR/mmv.h
cp ./projects/nav/hook/soundtouch/efftype.h $MM_DIR/efftype.h
cp ./projects/nav/mediafile/mfio_mp4.h $MM_DIR/mfio_mp4.h
cp ./projects/nav/netjointor/media_addr.h $MM_DIR/media_addr.h
cp ./projects/nav/netjointor/netdef.h $MM_DIR/netdef.h
cp ./projects/nav/android/src/libmm/mmapi.java $MM_DIR/mmapi.java
cp ./projects/nav/android/libs/armeabi-v7a/libmm.so $MM_DIR/libmm.so
cp ./projects/nav/ios/libmm.xcodeproj/bin/libmm.a $MM_DIR/libmm.a