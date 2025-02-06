
package libmedia;
import java.util.Hashtable;
import dalvik.system.BaseDexClassLoader;
import android.util.Log;
import android.os.Build;

public final class runtime
{
    public static final long omx_decode = 0;
    public static final long omx_encode = 1;
    public static final long bug_mask = 2;
    public static final long front_angle = 3;
    public static final long camera_csp = 4;
    public static final long back_angle = 5;
    public static final long omx_usurp = 6;

    private static final long bug_egl_release = (1 << 0);
    private static final long bug_egl_dettach = (1 << 1);
    private static final long bug_dlclose = (1 << 2);
    private static final long bug_nv12_nv21 = (1 << 3);
    private static final long bug_gl_flush = (1 << 4);
    private static final long bug_gl_no_resize = (1 << 5);
    private static final long bug_gl_clear = (1 << 6);
    private static final long bug_gl_clear2 = (1 << 7);

    public static int audio_stream = android.media.AudioManager.STREAM_VOICE_CALL;
    private static String phone = String.format("%s_%s", Build.MANUFACTURER, Build.MODEL);
    public static final int delay = delay_init();
    public static final String path = path_init();

    private static int delay_init() {
        Log.e("zylthinking", "phone: " + phone + "\n");

        Hashtable<String, Integer> values = new Hashtable<String, Integer>();
        values.put("LGE_LG_D802",            Integer.valueOf(420));
        values.put("HTC_HTC 802t 16GB",      Integer.valueOf(256));
        values.put("Meizu_M040",             Integer.valueOf(197));
        values.put("samsung_GT-I9220",       Integer.valueOf(240));
        values.put("Xiaomi_MI 2S",           Integer.valueOf(286));
        values.put("HUAWEI_HUAWEI P6 S-U06", Integer.valueOf(287));
        values.put("Xiaomi_HM NOTE 1TD",     Integer.valueOf(195));
        values.put("samsung_GT-I9300",       Integer.valueOf(208));
        values.put("Sony_L50u",              Integer.valueOf(350));
        values.put("samsung_GT-I9268",       Integer.valueOf(180));
        values.put("Meizu_MX4",              Integer.valueOf(380));
        values.put("Samsung_SM-G9006V",      Integer.valueOf(350));
        values.put("ONEPLUS_A0001",          Integer.valueOf(300));
        values.put("LGE_Nexus 5",            Integer.valueOf(210));
        values.put("HUAWEI_HUAWEI G750-T00", Integer.valueOf(257));
        values.put("Xiaomi_HM 1SC",          Integer.valueOf(445));
        values.put("LGE_Nexus 4",            Integer.valueOf(130));
        values.put("samsung_SM-N9006",       Integer.valueOf(228));

        int n = 120;
        if (values.containsKey(phone)) {
            n = values.get(phone).intValue();
        }
        return n;
    }

    private static String path_init() {
        BaseDexClassLoader loader = (BaseDexClassLoader) Thread.currentThread().getContextClassLoader();
        String so = loader.findLibrary("runtime");

        System.load(so);
        String path = so.substring(0, so.lastIndexOf("/") + 1);
        libpath_set(path.getBytes());

        if (0 == Build.MODEL.compareTo("ZTE V987")) {
            feature_mask(omx_decode, 0);
        } else if (0 == phone.compareTo("asus_ASUS_T00J")) {
            feature_mask(omx_decode, -1);
            feature_mask(bug_mask, bug_egl_release);
        } else if (0 == phone.compareTo("samsung_SCH-N719")) {
            feature_mask(omx_encode, 0);
            feature_mask(bug_mask, bug_egl_release);
        } else if (0 == phone.compareTo("samsung_SCH-I959") ||
                   0 == phone.compareTo("samsung_GT-I9220") ||
                   0 == phone.compareTo("Xiaomi_MI 2SC") ||
                   0 == phone.compareTo("samsung_GT-I8730")) {
            feature_mask(bug_mask, bug_egl_release);
        } else if (0 == Build.MODEL.compareTo("Nexus 6")) {
            feature_mask(front_angle, 1);
        } else if(0 == phone.compareTo("samsung_GT-I8268")) {
            feature_mask(omx_encode, 0);
            feature_mask(bug_mask, bug_egl_release | bug_egl_dettach);
        } else if (0 == phone.compareTo("samsung_GT-I8552")) {
            feature_mask(bug_mask, bug_egl_release | bug_dlclose | bug_nv12_nv21);
        } else if (0 == phone.compareTo("samsung_SM-N9100")) {
            feature_mask(bug_mask, bug_egl_dettach | bug_gl_flush);
        } else if (0 == phone.compareTo("YuLong_dazen X7")) {
            feature_mask(omx_usurp, 10);
            feature_mask(bug_mask, bug_gl_clear | bug_gl_clear2);
        } else if (0 == phone.compareTo("HUAWEI_HONOR H30-L01")) {
            feature_mask(bug_mask, bug_gl_no_resize);
        } else if (0 == phone.compareTo("Sony_L36h")) {
            feature_mask(bug_mask, bug_gl_clear2);
        }
        return path;
    }

    public static native void feature_mask(long key, long val);
    private static native void libpath_set(byte[] path);
}
