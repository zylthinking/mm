
package libmedia;
import android.util.Log;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.hardware.Camera;
import android.opengl.GLES20;
import android.graphics.ImageFormat;
import android.os.Handler;
import android.os.Message;
import android.os.Looper;
import android.os.Process;
import android.graphics.SurfaceTexture;
import java.lang.Thread;
import java.util.List;
import java.util.concurrent.Semaphore;
import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGL11;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.egl.EGLSurface;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.egl.EGLConfig;

class render_thread extends Thread {
    private long something = 0;
    private int nb = 0;
    private AudioTrack track = null;
    private int frame_ms = 40;

    public render_thread(int sample_rate, int channel, int bits, long something) {
        int frame_bytes = (frame_ms * sample_rate * channel * bits / 8000);

        bits = AudioFormat.ENCODING_PCM_16BIT;
        if (channel == 1) {
            channel = AudioFormat.CHANNEL_OUT_MONO;
        } else {
            channel = AudioFormat.CHANNEL_OUT_STEREO;
        }

        nb = AudioTrack.getMinBufferSize(sample_rate, channel, bits);
        if (nb < frame_bytes) {
            nb = frame_bytes;
        }

        track = new AudioTrack(runtime.audio_stream,
                               sample_rate, channel, bits, nb, AudioTrack.MODE_STREAM);
        nb = frame_bytes;
        this.something = something;
    }

    public void run() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO);
        byte[] buffer = new byte[nb + 640];
        track.play();

        while (true) {
            int delay = -1;
            if (runtime.delay != 0) {
                delay = 0;
            }

            int length = media.pcm_pull(something, buffer, 640, delay);
            if (length == -1) {
                break;
            }

            if (length == 0) {
                try {
                    Thread.sleep(5);
                } catch (Throwable t) {}
            } else {
                track.write(buffer, 0, length);
            }
        }

        track.stop();
        track.release();
        track = null;
        media.handle_dereference(something);
        Log.e("zylthinking", "audio play finished");
    }
}

class capture_thread extends Thread {
    private long something = 0;
    private int nb = 0;
    private AudioRecord recorder = null;
    private int frame_ms = 40;
    public int[] format = {0, 0};

    public capture_thread(int sample_rate, int channel, int bits, long something) throws Exception {
        format[0] = sample_rate;
        format[1] = channel;

        int frame_bytes = (frame_ms * sample_rate * channel * bits / 8000);
        bits = AudioFormat.ENCODING_PCM_16BIT;
        if (channel == 1) {
            channel = AudioFormat.CHANNEL_IN_MONO;
        } else {
            channel = AudioFormat.CHANNEL_IN_STEREO;
        }

        nb = AudioRecord.getMinBufferSize(sample_rate, channel, bits);
        if (nb < frame_bytes) {
            nb = frame_bytes;
        }

        recorder = new AudioRecord(MediaRecorder.AudioSource.MIC, sample_rate, channel, bits, nb);
        if (recorder.getState() == AudioRecord.STATE_UNINITIALIZED) {
            recorder.release();
            throw new Exception("recorder device initialize");
        }
        nb = frame_bytes;
        this.something = something;
    }

    public void run() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO);
        byte[] buffer = new byte[nb];
        recorder.startRecording();

        while (true) {
            int delay = -1;
            if (runtime.delay != 0) {
                delay = runtime.delay;
            }

            int length = recorder.read(buffer, 0, buffer.length);
            if (length > 0) {
                length = media.pcm_push(something, buffer, delay);
            } else {
                length = 0;
            }

            if (length < 0) {
                break;
            }
        }

        recorder.stop();
        recorder.release();
        recorder = null;
        media.handle_dereference(something);
        Log.e("zylthinking", "audio record finished");
    }
}

class video_message_handler extends Handler {
    private video_cap_thread obj = null;
    public video_message_handler(video_cap_thread obj) {
        this.obj = obj;
    }

    public void handleMessage(Message msg) {
        int n = 0;
        switch (msg.what) {
            case 0: obj.timer_expires(this); break;
            default: Log.e("zylthinking", "impossible cmd " + msg.arg1 + " issued"); break;
        }
    }
}

class video_cap_thread extends Thread implements SurfaceTexture.OnFrameAvailableListener, Camera.PreviewCallback {
    private static final int csp_any = 0;
    private static final int csp_bgra = 1;
    private static final int csp_rgb = 2;
    private static final int csp_i420 = 3;
    private static final int csp_gbrp = 4;
    private static final int csp_nv12 = 5;
    private static final int csp_nv12ref = 6;
    private static final int csp_nv21 = 7;
    private static final int csp_nv21ref = 8;
    private static final int csp_i420ref = 9;

    private static final int campos_back = 0;
    private static final int campos_front = 1;

    private static EGL10 egl = null;
    private static EGLDisplay display = null;
    private static EGLSurface surface = null;
    private static EGLContext context = null;
    private static int texname = 0;
    private SurfaceTexture texture = null;
    private int buffer_size = 0;

    private int[] campos = {-1, 0};
    private int width = 0;
    private int height = 0;
    private int fps = 0;
    private int csp = 0;
    public int[] strides = null;
    private long something = -1;
    private int drop_one = 0;
    private int nr = 0;

    private Camera camera = null;
    private Semaphore sem = null;
    private video_message_handler handler = null;

    public video_cap_thread(int campos, int width, int height, int csp, int fps, long something) {
        this.campos[1] = campos;
        this.width = width;
        this.height = height;
        this.fps = fps * 1000;
        this.csp = csp;
        this.something = something;
        this.sem = new Semaphore(1);

        while (true) {
            try {
                sem.acquire(1);
                break;
            } catch (Exception e) {
                continue;
            }
        }
    }

    private int start_camera() {
        int pos = Camera.CameraInfo.CAMERA_FACING_BACK;
        if (campos[1] == campos_front) {
            pos = Camera.CameraInfo.CAMERA_FACING_FRONT;
        }

        try {
            camera = Camera.open(pos);
        } catch(Throwable t) {
            return -1;
        }

        Camera.Parameters params = camera.getParameters();
        int[] intp = {0, 0};
        List<Camera.Size> sizes = params.getSupportedPreviewSizes();
        find_picture_size(sizes, this.width, this.height, intp);
        if (intp[0] == 0 || intp[1] == 0) {
            camera.release();
            camera = null;
            return -1;
        }
        params.setPreviewSize(intp[0], intp[1]);

        List<Integer> formats = params.getSupportedPreviewFormats();
        int fmt = find_format(formats, this.csp);
        if (fmt == 0) {
            camera.release();
            camera = null;
            return -1;
        }
        params.setPreviewFormat(fmt);

        strides = stride_of_panes(intp, fmt);
        if (strides == null) {
            camera.release();
            camera = null;
            return -1;
        }

        buffer_size = calc_buffer_size(strides, fmt);
        if (buffer_size == -1) {
            camera.release();
            camera = null;
            return -1;
        }

        params.setPreviewFpsRange(this.fps, this.fps);
        try {
            camera.setParameters(params);
        } catch (Throwable t) {
            List<int[]> range = params.getSupportedPreviewFpsRange();
            int[] fps = find_fps(range, this.fps);
            if (fps != null) {
                params.setPreviewFpsRange(fps[0], fps[1]);
                camera.setParameters(params);
                if (fps[0] == fps[1] && fps[0] > this.fps) {
                    drop_one = fps[0] - this.fps;
                    drop_one = (fps[0] + drop_one - 1) / drop_one;
                } else {
                    Log.e("zylthinking", "have to use range " + fps[0] + ":" + fps[1] + " for camera");
                }
            } else {
                camera.release();
                camera = null;
                return -1;
            }
        }

        if (-1 == reinit_camera(false)) {
            camera.release();
            camera = null;
            return -1;
        }

        try {
            params.setFocusMode(Camera.Parameters.FOCUS_MODE_CONTINUOUS_PICTURE);
            camera.cancelAutoFocus();
            camera.setParameters(params);
        } catch (Throwable t) {}

        camera.addCallbackBuffer(new byte[buffer_size]);
        camera.addCallbackBuffer(new byte[buffer_size]);
        camera.setPreviewCallbackWithBuffer(this);
        camera.startPreview();
        campos[0] = campos[1];
        return 0;
    }

    public int[] stride_of_panes(int[] intp, int csp) {
        int[] buf = null;
        switch (csp) {
            case ImageFormat.NV21:
                buf = new int[4];
                buf[0] = buf[1] = (intp[0] + 15) & (~15);
                buf[2] = intp[0];
                buf[3] = intp[1];
                break;
            case ImageFormat.YV12:
                buf = new int[5];
                buf[0] = (intp[0] + 15) & (~15);
                buf[1] = buf[2] = (intp[0] / 2 + 15) & (~15);
                buf[3] = intp[0];
                buf[4] = intp[1];
                break;
            default:
                Log.e("zylthinking", "does not support ImageFormat: " + csp);
        }
        return buf;
    }

    private int calc_buffer_size(int[] intp, int fmt) {
        if (fmt == ImageFormat.NV21) {
            return (intp[0] * intp[3]) + (intp[1] * intp[3] / 2);
        }

        if (fmt == ImageFormat.YV12) {
            return intp[0] * intp[4] + (intp[1] / 2) * (intp[4] / 2) + (intp[2] / 2) * (intp[4] / 2);
        }

        Log.e("zylthinking", "calc_buffer_size does not support format " + fmt);
        return -1;
    }

    public void onPreviewFrame(byte[] data, Camera camera) {
        if (drop_one != 0 && (++nr) == drop_one) {
            nr = 0;
            camera.addCallbackBuffer(data);
            return;
        }

        int n = media.pixel_push(something, data);
        if (n == -1) {
            Looper looper = handler.getLooper();
            looper.quit();
        } else {
            camera.addCallbackBuffer(new byte[buffer_size]);
        }
    }

    public void onFrameAvailable(SurfaceTexture texture) {
        try {
            texture.updateTexImage();
        } catch (Throwable t) {}
    }

    private void stop_camera() {
        if (campos[0] != -1) {
            camera.stopPreview();
            egl.eglMakeCurrent(display, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_CONTEXT);
            if (texture != null) {
                texture.release();
                texture = null;
            }
            campos[0] = -1;
        }
    }

    private int reinit_camera(boolean need_run) {
        stop_camera();

        if (context != null) {
            if (texname != 0) {
                int[] intp = {texname};
                GLES20.glDeleteTextures(1, intp, 0);
                texname = 0;
            }
            egl.eglDestroyContext(display, context);
            context = null;
        }

        texname = egl_initialize(texname);
        if (texname == 0) {
            return -1;
        }

        Camera.Parameters params = camera.getParameters();
        try {
            texture = new SurfaceTexture(texname);
            camera.setPreviewTexture(texture);
        } catch (Exception e) {
            texture = null;
            return -1;
        }

        texture.setOnFrameAvailableListener(this);
        if (need_run) {
            camera.startPreview();
            campos[0] = campos[1];
        }
        return 0;
    }

    public boolean camera_start_failed() {
        while (true) {
            try {
                sem.acquire(1);
                break;
            } catch (Exception e) {
                continue;
            }
        }
        sem = null;
        return (campos[0] == -1);
    }

    public void timer_expires(video_message_handler handler) {
        if (!egl.eglMakeCurrent(display, surface, surface, context)) {
            int n = egl.eglGetError();
            if (n == EGL11.EGL_CONTEXT_LOST) {
                reinit_camera(true);
            } else {
                Log.e("zylthinking", "eglMakeCurrent failed with " + n);
            }
        }
        handler.sendEmptyMessageDelayed(0, 1000);
    }

    public void run() {
        Looper.prepare();
        handler = new video_message_handler(this);
        if (!handler.sendEmptyMessageDelayed(0, 1000)) {
            sem.release();
            handler = null;
            return;
        }

        int n = start_camera();
        sem.release();
        if (-1 == n) {
            handler = null;
            return;
        }

        Looper.loop();
        stop_camera();
        camera.release();
        camera = null;
        handler = null;
        media.handle_dereference(something);
    }

    private int find_format(List<Integer> formats, int csp) {
        switch (csp) {
            case video_cap_thread.csp_nv21:
            case video_cap_thread.csp_nv21ref:
                csp = ImageFormat.NV21;
                break;
            default:
                return 0;
        }

        for (int i = 0; i < formats.size(); ++i) {
            Integer format = formats.get(i);
            Log.e("zylthinking", "ImageFormat " + format + " supported\n");
        }

        for (int i = 0; i < formats.size(); ++i) {
            Integer format = formats.get(i);
            if (format.intValue() == csp) {
                return csp;
            }
        }
        return 0;
    }

    private int[] find_fps(List<int[]> range, int fps) {
        if (range == null || range.size() < 1) {
            return null;
        }

        int[] s = null;
        for (int i = 0; i < range.size(); ++i) {
            int[] r = range.get(i);
            if (r[0] != r[1]) {
                continue;
            }

            if (r[0] == fps) {
                return r;
            }

            if (s == null) {
                s = r;
            } else if (r[0] > fps) {
                if (s[0] > r[0]) {
                    s = r;
                }
            } else if (r[0] > s[0]) {
                s = r;
            }
        }

        if (s != null && s[0] * 10 > fps * 8) {
            return s;
        }
        s = null;

        for (int i = 0; i < range.size(); ++i) {
            int[] r = range.get(i);
            if (r[0] <= fps && r[1] >= fps) {
                if (s == null) {
                   s = r;
                } else if (s[0] < r[0] && s[1] > r[1]) {
                    s = r;
                } else if ((s[0] * s[1]) > (r[0] * r[1])) {
                    s = r;
                }
            }
        }

        if (s != null) {
            return s;
        }

        s = range.get(0);
        int nearest = 0;
        if (s[0] > fps) {
            nearest = s[0];
        } else {
            nearest = s[1];
        }
        int differ = nearest - fps;

        for (int i = 0; i < range.size(); ++i) {
            int[] r = range.get(i);
            if (r[0] > fps) {
                int n = r[0] - fps;
                if (n * n < differ * differ) {
                    nearest = r[0];
                    differ = nearest - fps;
                    s = r;
                }
            } else {
                int n = r[1] - fps;
                if (n * n < differ * differ) {
                    nearest = r[1];
                    differ = nearest - fps;
                    s = r;
                }
            }
        }
        return s;
    }

    private void find_picture_size(List<Camera.Size> list, int width, int height, int[] dim) {
        int i;

        for (i = 0; i < list.size(); ++i) {
            Camera.Size size = (Camera.Size) list.get(i);
            Log.e("zylthinking", "width " + size.width + ", height = " + size.height + " supported");
        }

        for (i = 0; i < list.size(); ++i) {
            Camera.Size size = (Camera.Size) list.get(i);
            if (size.width == width && size.height == height) {
                dim[0] = width;
                dim[1] = height;
                return;
            }
        }

        int w1 = Integer.MAX_VALUE;
        int h1 = Integer.MAX_VALUE;
        for (i = 0; i < list.size(); ++i) {
            Camera.Size size = (Camera.Size) list.get(i);
            if (size.width >= width && size.height >= height) {
                if (w1 >= size.width && h1 >= size.height) {
                    w1 = size.width;
                    h1 = size.height;
                    continue;
                }

                if (w1 * h1 > size.width * size.height) {
                    w1 = size.width;
                    h1 = size.height;
                    continue;
                }
            }
        }

        if (w1 != Integer.MAX_VALUE && h1 != Integer.MAX_VALUE) {
            dim[0] = w1;
            dim[1] = h1;
            return;
        }

        w1 = 0;
        h1 = 0;
        int w2 = 0;
        int h2 = 0;
        int w3 = 0;
        int h3 = 0;
        for (i = 0; i < list.size(); ++i) {
            Camera.Size size = (Camera.Size) list.get(i);
            if (size.width >= width) {
                if (size.height > h1) {
                    w1 = size.width;
                    h1 = size.height;
                    continue;
                }
            } else if (size.height >= height) {
                if (size.width > w2) {
                    w2 = size.width;
                    h2 = size.height;
                    continue;
                }
            } else {
                if (w3 * h3 < size.width * size.height) {
                    w3 = size.width;
                    h3 = size.height;
                }
            }
        }

        if (width * h1 > w2 * height) {
            dim[0] = w1;
            dim[1] = h1;
        } else if (w2 * height > width * h1) {
            dim[0] = w2;
            dim[1] = h2;
        } else {
            dim[0] = 0;
            dim[1] = 0;
        }
    }

    private static int egl_initialize(int name) {
        if (egl == null) {
            egl = (EGL10) EGLContext.getEGL();
            if (egl == null) {
                return 0;
            }
        }

        if (display == null) {
            display = egl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY);
            if (display == null || display == EGL10.EGL_NO_DISPLAY) {
                display = null;
                return 0;
            }
        }

        // reinitialize is allowed by egl spec
        if (!egl.eglInitialize(display, null)) {
            return 0;
        }

        int[] attr = {
            EGL10.EGL_SURFACE_TYPE, EGL10.EGL_PBUFFER_BIT,
            EGL10.EGL_RENDERABLE_TYPE, 4,
            EGL10.EGL_NONE
        };

        EGLConfig[] conf = new EGLConfig[1];
        if (!egl.eglChooseConfig(display, attr, conf, 1, new int[1])) {
            return 0;
        }

        if (surface == null) {
            int[] surface_attr = {
                EGL10.EGL_WIDTH, 1,
                EGL10.EGL_HEIGHT, 1,
                EGL10.EGL_NONE
            };

            surface = egl.eglCreatePbufferSurface(display, conf[0], surface_attr);
            if (surface == EGL10.EGL_NO_SURFACE) {
                return 0;
            }
        }

        if (context == null) {
            attr[0] = 0x3098;
            attr[1] = 2;
            attr[2] = EGL10.EGL_NONE;

            context = egl.eglCreateContext(display, conf[0], EGL10.EGL_NO_CONTEXT, attr);
            if (context == null || context == EGL10.EGL_NO_CONTEXT) {
                context = null;
                return 0;
            }
        }

        egl.eglMakeCurrent(display, surface, surface, context);
        if (name == 0) {
            int intp[] = new int[1];
            GLES20.glGenTextures(1, intp, 0);
            name = intp[0];
        }
        return name;
    }
}

public class media {
    private static int track_start(int sample_rate, int channel, int bits, long something) {
        if (bits != 16 || (channel != 1 && channel != 2)) {
            return -1;
        }

        try {
            render_thread render = new render_thread(sample_rate, channel, bits, something);
            render.start();
        } catch (Throwable t) {
            return -1;
        }
        return 0;
    }

    private static int[] record_start(int sample_rate, int channel, int bits, long something) {
        if (bits != 16 || (channel != 1 && channel != 2)) {
            return null;
        }

        capture_thread capture = null;
        try {
            capture = new capture_thread(sample_rate, channel, bits, something);
            capture.start();
        } catch (Throwable t) {
            return null;
        }
        return capture.format;
    }

    private static int[] video_start(int camepos, int width, int height, int csp, int fps, long something) {
        video_cap_thread capture = new video_cap_thread(camepos, width, height, csp, fps, something);
        capture.start();
        if (capture.camera_start_failed()) {
            return null;
        }
        return capture.strides;
    }

    static native void handle_dereference(long handle);
    static native int pcm_pull(long something, byte[] buf, int padding, int delay);
    static native int pcm_push(long something, byte[] buf, int delay);
    static native int pixel_push(long something, byte[] buf);
}
