
package libmm.demo2;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.RelativeLayout;
import android.widget.CompoundButton;
import android.widget.CompoundButton.OnCheckedChangeListener;
import android.widget.Button;
import android.widget.ToggleButton;
import android.widget.TextView;
import android.view.TextureView;
import android.view.Surface;
import android.graphics.SurfaceTexture;
import android.view.TextureView.SurfaceTextureListener;
import java.lang.Thread;
import java.lang.String;
import android.util.Log;
import android.media.AudioManager;
import android.content.Context;
import android.view.WindowManager;
import java.lang.ref.WeakReference;
import libmm.fogapi;
import libmedia.runtime;
import libmedia.surfacepool;

class netstat_thread extends Thread {
    private fog view = null;
    private long tracker;
    public netstat_thread(fog view, long tracker) {
        this.view = view;
        this.tracker = tracker;
    }

    public void run() {
        view.track(tracker);
    }
}

class surface_listener implements SurfaceTextureListener, Handler.Callback {
    public Object handle;
    private fog view = null;
    private Surface sfc = null;
    private Handler handler = null;
    private TextureView ui = null;
    private int width = 0;
    private int height = 0;

    public surface_listener(fog view, int id) {
        this.handler = new Handler(this);
        this.view = view;
        this.ui = (TextureView) view.findViewById(id);
        this.ui.setSurfaceTextureListener(this);
    }

    public void set_height(int h) {
        if (h == -1) {
            h = height;
        }
        RelativeLayout.LayoutParams param = (RelativeLayout.LayoutParams) ui.getLayoutParams();
        param.width = width;
        param.height = h;
        ui.setLayoutParams(param);
    }

    public boolean handleMessage(Message msg) {
        width = msg.arg1;
        height = msg.arg2;
        sfc = new Surface((SurfaceTexture) msg.obj);
        handle = surfacepool.add_surface(sfc, 255, 255, 255, width, height);
        return true;
    }

    public void onSurfaceTextureAvailable(SurfaceTexture st, int width, int height) {
        Message message = Message.obtain();
        message.obj = st;
        message.arg1 = width;
        message.arg2 = height;
        handler.sendMessage(message);
    }

    public boolean onSurfaceTextureDestroyed(SurfaceTexture surface) {
        Log.e("zylthinking", "onSurfaceTextureDestroyed");
        if (sfc != null) {
            surfacepool.remove_surface(sfc);
            sfc.release();
            sfc = null;
            handle = null;
        }
        return true;
    }

    public void onSurfaceTextureSizeChanged(SurfaceTexture surface, int width, int height) {
        if (handle != null) {
            surfacepool.surface_resize(handle, width, height);
        }
    }

    public void onSurfaceTextureUpdated(SurfaceTexture surface) {}
}

public class fog extends Activity {
    private Handler handler = null;
    private TextView panel = null;
    private Button sk = null;
    private ToggleButton m2 = null;
    private ToggleButton m1 = null;
    private ToggleButton sz = null;

    private long handle1 = 0;
    private long handle2 = 0;
    private surface_listener monitor1 = null;
    private String uri[] = {
        "rtmp://106.120.169.75/live_panda/0cffc046bbc4bb87d5f4b50949baec0a", // 0
        "http://ec.sinovision.net/video/ts/lv.m3u8", // 1
        "rtmp://106.120.169.75/live_camera/36020100201", // 2
        "rtmp://62.113.210.250:1935/medienasa-live/ok-magdeburg_high", // 3
        "rtmp://www.bj-mobiletv.com:8000/live/live1", // 4
        "rtmp://live.hkstv.hk.lxdns.com/live/hks", // 5
        "rtmp://ftv.sun0769.com/dgrtv1/mp4:b1", // 6
        "http://jq.v.ismartv.tv/cdn/1/81/95e68bbdce46b5b8963b504bf73d1b/normal/slice/index.m3u8", // 7
        "/sdcard/test.mp4", // 8
        "/sdcard/z.mp4" // 9
    };

    protected void onCreate(Bundle icicle) {
        runtime.audio_stream = AudioManager.STREAM_MUSIC;
        setVolumeControlStream(runtime.audio_stream);

        super.onCreate(icicle);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(R.layout.main);
        handler = new my_handler(new WeakReference<fog>(this));

        panel = (TextView) findViewById(R.id.status);
        m1 = (ToggleButton) findViewById(R.id.m1);
        sz = (ToggleButton) findViewById(R.id.sz);
        m2 = (ToggleButton) findViewById(R.id.m2);
        sk = (Button) findViewById(R.id.sk);
        monitor1 = new surface_listener(this, R.id.textureView);

        m1.setOnCheckedChangeListener(new OnCheckedChangeListener() {
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                if (isChecked) {
                    handle1 = fogapi.supplier_open(uri[4].getBytes(), 0, 1, 4000, 8000);
                    if (handle1 == 0) {
                        m1.setChecked(false);
                    } else {
                        long tracker = fogapi.supplier_wait_get(handle1);
                        new netstat_thread(fog.this, tracker).start();
                    }
                } else if (handle1 != 0) {
                    if (handle2 != 0) {
                        fogapi.supplier_close(handle2);
                        handle2 = 0;
                        m2.setChecked(false);
                    }
                    fogapi.supplier_close(handle1);
                    handle1 = 0;
                }
            }
        });

        m2.setOnClickListener(new OnClickListener() {
            public void onClick(View view) {
                if (handle2 == 0) {
                    if (handle1 == 0) {
                        m2.setChecked(false);
                        return;
                    }

                    handle2 = fogapi.supplier_open(uri[5].getBytes(), -1, 1, 2000, 40000);
                } else {
                    fogapi.supplier_close(handle2);
                    handle2 = 0;
                }
            }
        });

        sz.setOnCheckedChangeListener(new OnCheckedChangeListener() {
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                if (isChecked) {
                    monitor1.set_height(320);
                } else {
                    monitor1.set_height(-1);
                }
            }
        });

        sk.setOnClickListener(new OnClickListener() {
            public void onClick(View view) {
                if (handle1 != 0) {
                    long n = fogapi.supplier_seek(handle1, 80 * 1000);
                    if (n == -1) {
                        Log.e("zylthinking", "seek failed\n");
                    }
                }
            }
        });
    }

    private static class my_handler extends Handler {
        private WeakReference wref = null;
        public my_handler(WeakReference wref) {
            this.wref = wref;
        }

        public void handleMessage(Message msg) {
            fog obj = (fog) wref.get();
            if (obj != null) {
                obj.panel.setText((String) msg.obj);
            }
        }
    }

    private void post_message(String text) {
        Message message = Message.obtain();
        message.obj = text;
        handler.sendMessage(message);
    }

    public void track(long tracker) {
        while (true) {
            long[] ctx = fogapi.supplier_context(tracker);
            if (ctx == null) {
                post_message("tracker exit");
                break;
            }

            String msg = String.format("%d:%d, %d:%d\n", ctx[0], ctx[1], ctx[2], ctx[3]);
            post_message(msg);
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {}
        }
        fogapi.supplier_wait_put(tracker);
    }
}
