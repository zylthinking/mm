
package libmedia;
import android.view.Surface;
import android.util.Log;
import java.util.LinkedList;
import java.util.concurrent.locks.ReentrantLock;

class pool_item {
    int width;
    int height;
    Surface sfc;
    long handle;
    long z0;
    long z1;
}

public final class surfacepool
{
    private static LinkedList<pool_item> pool = new LinkedList<pool_item>();
    private static ReentrantLock lck = new ReentrantLock();

    public static pool_item add_surface(Surface sfc, int r, int g, int b, int width, int height) {
        pool_item item = new pool_item();
        item.width = width;
        item.height = height;
        item.sfc = sfc;
        item.z0 = 0;
        item.z1 = 0;
        item.handle = surface_register(r, g, b, width, height, sfc);
        if (item.handle == 0) {
            return null;
        }

        lck.lock();
        pool.add(item);
        lck.unlock();
        return item;
    }

    public static void surface_resize(Object handle, int width, int height) {
        pool_item item = (pool_item) handle;
        item.width = width;
        item.height = height;
        surface_resize(item.handle, item.width, item.height);
    }

    public static void remove_surface(Surface sfc) {
        pool_item item = null;

        lck.lock();
        for (int i = 0; i < pool.size(); ++i) {
            pool_item entry = pool.get(i);
            if (entry.sfc == sfc) {
                item = entry;
                pool.remove(i);
            }
        }
        lck.unlock();

        if (item != null) {
            surface_unregister(item.handle);
        }
    }

    private static long[] area_open(long cid, long uid, long w, long h) {
        long[] xywhz = new long[6];
        lck.lock();

        try {
            pool_item item = area_open(cid, uid, w, h, xywhz);
            if (item == null) {
                xywhz = null;
            } else {
                xywhz[5] = handle_reference(item.handle);
            }
        } catch (Throwable t) {
            xywhz = null;
        } finally {
            lck.unlock();
        }
        return xywhz;
    }

    private final static pool_item multi_view_area_open(long cid, long uid, long w, long h, long[] xywhz) {
        pool_item entry = null;
        if (0 != cid) {
            entry = pool.get(0);
        } else {
            entry = pool.get(1);
        }

        xywhz[0] = 0;
        xywhz[1] = 0;
        xywhz[2] = entry.width;
        xywhz[3] = entry.height;
        xywhz[4] = 0;
        entry.z0 = uid;
        return entry;
    }

    private final static pool_item area_open(long cid, long uid, long w, long h, long[] xywhz) {
        //Log.e("zylthinking", "you must implement this method to return a Surface for video " + id);
        //Log.e("zylthinking", "xywh means (x, y, width, height) which will be the displaying area in the surface");
        //Log.e("zylthinking", "and z means z coordinate, must be 0 or 1, 1 is nearer to your eye");
        //Log.e("zylthinking", "you can select an existing Surface or create a new one");
        //Log.e("zylthinking", "notice: it is a ReentrantLock, meaning you can call add_surface here");
        if (pool.size() == 0) {
            return null;
        } else if (pool.size() != 1) {
            return multi_view_area_open(cid, uid, w, h, xywhz);
        }

        pool_item entry = pool.get(0);
        if (entry.z0 != 0 && entry.z1 != 0) {
            return null;
        }

        if (entry.width * h > entry.height * w) {
            w = w * entry.height / h;
            h = entry.height;
        } else {
            h = h * entry.width / w;
            w = entry.width;
        }

        if (entry.z0 == 0 && (0 != cid || entry.z1 != 0)) {
            xywhz[0] = (entry.width - w) / 2;
            xywhz[1] = (entry.height - h) / 2;
            xywhz[2] = w;
            xywhz[3] = h;
            xywhz[4] = 0;
            entry.z0 = uid;
        } else if (entry.z1 == 0) {
            xywhz[0] = entry.width / 2;
            xywhz[1] = 0;
            xywhz[2] = entry.width / 2;
            xywhz[3] = entry.height / 2;
            xywhz[4] = 1;
            entry.z1 = uid;
        }
        return entry;
    }

    private static long[] area_reopen(long z, long w, long h, long width, long height) {
        long[] xywhz = new long[5];
        if (width * h > height * w) {
            w = w * height / h;
            h = height;
        } else {
            h = h * width / w;
            w = width;
        }

        if (z == 0) {
            xywhz[0] = (width - w) / 2;
            xywhz[1] = (height - h) / 2;
            xywhz[2] = w;
            xywhz[3] = h;
            xywhz[4] = 0;
        } else {
            xywhz[0] = width / 2;
            xywhz[1] = 0;
            xywhz[2] = width / 2;
            xywhz[3] = height / 2;
            xywhz[4] = 1;
        }
        return xywhz;
    }

    private static int area_close(long cid, long uid, long handle) {
        handle_dereference(handle);

        lck.lock();
        for (int i = 0; i < pool.size(); ++i) {
            pool_item entry = pool.get(i);
            if (entry.z0 == uid) {
                entry.z0 = 0;
                break;
            }

            if (entry.z1 == uid) {
                entry.z1 = 0;
                break;
            }
        }
        lck.unlock();

        //Log.e("zylthinking", "return 0 the picture will remains until a surface_refresh");
        //Log.e("zylthinking", "return 1 the picture a surface_refresh will be called immediately");
        //Log.e("zylthinking", "which will cause all pictures remains by closed area being cleared");
        return 0;
    }

    private static native long handle_reference(long handle);
    private static native void handle_dereference(long handle);
    //private static native int area_modify(long id, long[] xywhz);
    private static native long surface_register(int r, int g, int b, int width, int height, Object sfc);
    private static native void surface_resize(long handle, long width, long height);
    private static native void surface_unregister(long handle);

    static {
        System.load(runtime.path + "libmedia2.so");
    }
}
