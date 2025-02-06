
package libmm;
import libmedia.runtime;

public final class mp4
{
    public static native long mp4_begin(byte[] file, long uid, long cid, long video);
    public static native void mp4_end(long mp4);

    static {
        System.load(runtime.path + "libmedia2.so");
        System.load(runtime.path + "libmp4.so");
    }
}
