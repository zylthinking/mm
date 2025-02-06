
package libmm;
import libmedia.runtime;

public class fogapi {
    public static native long supplier_open(byte[] uri, long seekto, long sync, long latch0, long latch1);
    public static native long[] supplier_context(long any);
    public static native long supplier_seek(long any, long pos);
    public static native long supplier_wait_get(long any);
    // only one thread is allowed to call supplier_wait at same time
    public static native int supplier_wait(long waiter);
    public static native void supplier_wait_put(long waiter);
    public static native void supplier_close(long any);

    static {
        System.load(runtime.path + "libmedia2.so");
        System.load(runtime.path + "libfog.so");
    }
}
