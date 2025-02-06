
package libmm;
import libmedia.runtime;

public class mmapi {
    public static final int campos_back = 0;
    public static final int campos_front = 1;

    public static final int audio_capture_start = 0;
    public static final int audio_capture_stop = 1;
    public static final int audio_capture_pause = 2;
    public static final int audio_play_pause = 3;
    public static final int audio_choose_aec = 4;
    public static final int audio_enable_dtx = 5;
    public static final int video_capture_start = 6;
    public static final int video_capture_stop = 7;
    public static final int video_use_camera = 8;

    public static final int aec_none = 0;
    public static final int aec_webrtc_none = 2;
    public static final int aec_webrtc_mb = 3;
    public static final int aec_webrtc_pc = 4;

    public static final int type_none = 0;
    public static final int type_random = 1;
    public static final int type_old = 2;
    public static final int type_male = 3;
    public static final int type_female = 4;
    public static final int type_child = 5;
    public static final int type_subtle = 6;

    public static final int notify_longs = 5;
    public static final int notify_disconnected = 2;
    public static final int notify_login = 3;
    public static final int notify_peer_stream = 4;
    public static final int notify_peer_count = 5;
    public static final int notify_peer_stat = 6;
    public static final int notify_statistic = 7;
    public static final int notify_io_stat = 8;

    public static native int nav_compile(byte[] bin, int sid, byte[] ip, int port);
    public static native long nav_open(long uid, byte[] info, int notifys, int level, int isp, int ms_wait, int dont_play, int dont_upload);
    public static native void nav_close(long any, int code);
    public static native int nav_ioctl(long any, int cmd, int[] args);
    public static native long[] nav_status();

    public static native long mmv_captofile(byte[] path, int effect, int aec);
    public static native int mmv_cap_amplitude(long any);
    public static native void mmv_capstop(long any);

    public static native long mmv_playfile(byte[] path);
    public static native long[] mmv_playpos(long any);
    public static native void mmv_stoplay(long any);

    static {
        System.load(runtime.path + "libmedia2.so");
        System.load(runtime.path + "libmm.so");
    }
}
