
package libmm.demo1;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.FrameLayout;
import android.widget.CompoundButton;
import android.widget.CompoundButton.OnCheckedChangeListener;
import android.widget.Button;
import android.widget.ToggleButton;
import android.widget.TextView;
import android.widget.EditText;
import android.view.TextureView;
import android.view.Surface;
import android.graphics.SurfaceTexture;
import android.view.TextureView.SurfaceTextureListener;
import android.graphics.Bitmap;
import java.lang.Thread;
import java.lang.String;
import android.util.Log;
import android.media.AudioManager;
import android.content.Context;
import android.os.Build;
import android.view.WindowManager;
import java.lang.ref.WeakReference;
import libmm.mmapi;
import libmm.mp4;
import libmedia.surfacepool;
import libmedia.runtime;

class netstat_thread extends Thread {
    private mmv view = null;
    public netstat_thread(mmv view) {
        this.view = view;
    }

    public void run() {
        view.track();
    }
}

class surface_listener implements SurfaceTextureListener, Handler.Callback {
    public Object handle;
    private mmv view = null;
    private Surface sfc = null;
    private Handler handler = null;
    private TextureView ui = null;
    private int width = 0;
    private int height = 0;

    public surface_listener(mmv view, int id) {
        this.handler = new Handler(this);
        this.view = view;
        this.ui = (TextureView) view.findViewById(id);
        this.ui.setSurfaceTextureListener(this);
    }

    public void set_size(int w, int h) {
        if (h == -1) {
            h = height;
        }

        if (w == -1) {
            w = width;
        }

        FrameLayout.LayoutParams param = (FrameLayout.LayoutParams) ui.getLayoutParams();
        param.width = w;
        param.height = h;
        ui.setLayoutParams(param);
    }

    public boolean handleMessage(Message msg) {
        if (msg.obj == null) {
            surfacepool.remove_surface(sfc);
            sfc.release();
            sfc = null;
            handle = null;
        } else {
            width = msg.arg1;
            height = msg.arg2;
            sfc = new Surface((SurfaceTexture) msg.obj);
            handle = surfacepool.add_surface(sfc, 255, 255, 255, width, height);
        }
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
            Message message = Message.obtain();
            message.obj = null;
            message.arg1 = 0;
            message.arg2 = 0;
            handler.sendMessage(message);
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

public class mmv extends Activity {
    private long handle = -1;
    private long play_handle = -1;
    private long cap_handle = -1;
    private int speaker = 0;
    private int aec_type = mmapi.aec_webrtc_mb;
    private long uid = System.currentTimeMillis() % 10000;
    private int campos = mmapi.campos_front;

    private long peer = 0;
    private long cid = 0;
    private long pfcc = 0;
    private long mp4_handle = 0;

    private Handler handler = null;
    private TextView panel = null;
    private Button aecbutton = null;
    private Button routebutton = null;
    private Button snapbutton = null;

    private ToggleButton testbutton = null;
    private ToggleButton capbutton = null;
    private ToggleButton netbutton = null;
    private ToggleButton audbutton = null;
    private ToggleButton vidbutton = null;
    private ToggleButton hdbuttton = null;
    private ToggleButton hdbuttton2 = null;
    private ToggleButton szbutton = null;
    private ToggleButton backbutton = null;
    private ToggleButton mp4button = null;
    private surface_listener monitor1 = null;
    private surface_listener monitor2 = null;

    private int aec_get(int speaker) {
        if (speaker == 0) {
            return mmapi.aec_webrtc_none;
        }
        return aec_type;
    }

    protected void switchMode(boolean speaker_mode) {
        AudioManager audioMgr = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        if (speaker_mode) {
            audioMgr.setSpeakerphoneOn(true);
            Log.e("zylthinking", "speaker\n");
        } else {
            audioMgr.setSpeakerphoneOn(false);
            Log.e("zylthinking", "phone\n");
        }
   }

    private byte[] compile_from_ui() {
        EditText edit = (EditText) findViewById(R.id.ip);
        String ip = edit.getText().toString();
        if (ip.length() == 0) {
            return null;
        }

        edit = (EditText) findViewById(R.id.port);
        String port = edit.getText().toString();
        if (port.length() == 0) {
            return null;
        }

        edit = (EditText) findViewById(R.id.sid);
        String sid = edit.getText().toString();
        if (sid.length() == 0) {
            return null;
        }

        byte[] bin = new byte[1024];
        int n = mmapi.nav_compile(bin, Integer.parseInt(sid),
                ip.getBytes(), Integer.parseInt(port));
        byte[] ret = new byte[n];
        System.arraycopy(bin, 0, ret, 0, n); 
        return ret;
    }

    private void reset_button_stat() {
        netbutton.setEnabled(cap_handle == -1);
        if (handle == -1) {
            audbutton.setChecked(false);
            vidbutton.setChecked(false);
        }
        audbutton.setEnabled(handle != -1);
        vidbutton.setEnabled(handle != -1);

        capbutton.setEnabled(handle == -1 && !testbutton.isChecked());
        testbutton.setEnabled(handle == -1 && !capbutton.isChecked());
        aecbutton.setEnabled(true);
        routebutton.setEnabled(true);
    }

    protected void onCreate(Bundle icicle) {
        super.onCreate(icicle);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        runtime.audio_stream = AudioManager.STREAM_VOICE_CALL;
        setVolumeControlStream(runtime.audio_stream);

        setContentView(R.layout.main);
        panel = (TextView) findViewById(R.id.status);
        netbutton = (ToggleButton) findViewById(R.id.conn);
        audbutton = (ToggleButton) findViewById(R.id.aud);
        vidbutton = (ToggleButton) findViewById(R.id.vid);
        hdbuttton = (ToggleButton) findViewById(R.id.hd);
        hdbuttton2 = (ToggleButton) findViewById(R.id.hd2);
        szbutton = (ToggleButton) findViewById(R.id.sz);
        backbutton = (ToggleButton) findViewById(R.id.back);

        capbutton = (ToggleButton) findViewById(R.id.cap);
        mp4button = (ToggleButton) findViewById(R.id.rc);
        testbutton = (ToggleButton) findViewById(R.id.test);
        testbutton.setVisibility(View.GONE);
        aecbutton = (Button) findViewById(R.id.aec);
        aecbutton.setVisibility(View.GONE);
        routebutton = (Button) findViewById(R.id.route);
        snapbutton = (Button) findViewById(R.id.snap);

        monitor1 = new surface_listener(this, R.id.textureView);
        //monitor2 = new surface_listener(this, R.id.textureView2);
        reset_button_stat();

        netbutton.setOnCheckedChangeListener(new OnCheckedChangeListener() {
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                if (isChecked) {
                    byte[] bin = compile_from_ui();
                    if (bin == null) {
                        netbutton.setChecked(false);
                    } else {
                        handle = mmapi.nav_open(uid, bin, 72, 2, 0, 1000, 0, 0);
                        if (handle == -1) {
                            netbutton.setChecked(false);
                        } else {
                            new netstat_thread(mmv.this).start();
                        }
                    }
                } else if (handle != -1) {
                    mmapi.nav_close(handle, 0);
                    handle = -1;
                }
                reset_button_stat();
            }
        });

        audbutton.setOnCheckedChangeListener(new OnCheckedChangeListener() {
            boolean on = false;

            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                if (isChecked) {
                    int aec = aec_get(speaker);
                    int[] args = {aec, mmapi.type_none, 8000, 1};
                    int n = mmapi.nav_ioctl(handle, mmapi.audio_capture_start, args);
                    if (n == -1) {
                        audbutton.setChecked(false);
                    } else {
                        on = true;
                        switchMode(speaker == 1);
                    }
                } else if (on) {
                    if (handle != -1) {
                        mmapi.nav_ioctl(handle, mmapi.audio_capture_stop, null);
                    }
                    on = false;
                }
            }
        });

        vidbutton.setOnCheckedChangeListener(new OnCheckedChangeListener() {
            boolean on = false;

            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                if (isChecked) {
                    int[] args = {1, 480, 640, campos, 15, 25, 300};
                    int n = mmapi.nav_ioctl(handle, mmapi.video_capture_start, args);
                    if (n == -1) {
                        vidbutton.setChecked(false);
                    } else {
                        on = true;
                    }
                } else if (on) {
                    if (handle != -1) {
                        mmapi.nav_ioctl(handle, mmapi.video_capture_stop, null);
                    }
                    on = false;
                }
            }
        });

        hdbuttton.setOnCheckedChangeListener(new OnCheckedChangeListener() {
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                runtime.feature_mask(runtime.omx_encode, isChecked ? 1 : 0);
            }
        });

        hdbuttton2.setOnCheckedChangeListener(new OnCheckedChangeListener() {
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                runtime.feature_mask(runtime.omx_decode, isChecked ? 1 : 0);
            }
        });

        backbutton.setOnCheckedChangeListener(new OnCheckedChangeListener() {
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                int pos = isChecked ? mmapi.campos_back : mmapi.campos_front;
                if (handle != -1) {
                    int[] args = {pos};
                    int n = mmapi.nav_ioctl(handle, mmapi.video_use_camera, args);
                    if (n == 0) {
                        campos = pos;
                    } else {
                        backbutton.setChecked(!isChecked);
                    }
                } else {
                    backbutton.setChecked(!isChecked);
                }
            }
        });

        routebutton.setOnClickListener(new OnClickListener() {
            public void onClick(View view) {
                if (handle != -1) {
                    int aec = aec_get(speaker == 1 ? 0 : 1);

                    int[] args = {aec, 32768, 65535};
                    if (speaker == 1) {
                        args[1] = 65535;
                    }

                    int n = mmapi.nav_ioctl(handle, mmapi.audio_choose_aec, args);
                    if (n == -1) {
                        return;
                    }
                }

                if (speaker == 1) {
                    speaker = 0;
                    routebutton.setText("听筒");
                    switchMode(false);
                } else {
                    speaker = 1;
                    routebutton.setText("外放");
                    switchMode(true);
                }
            }
        });

        aecbutton.setOnClickListener(new OnClickListener() {
            public void onClick(View view) {
                int type = mmapi.aec_webrtc_mb;
                if (aec_type == mmapi.aec_webrtc_mb) {
                    type = mmapi.aec_webrtc_pc;
                }

                int n = 0;
                if (handle != -1) {
                    int[] args = {type, 32768, 65535};
                    if (speaker == 0) {
                        args[1] = 65535;
                    }

                    n = mmapi.nav_ioctl(handle, mmapi.audio_choose_aec, args);
                    if (n == -1) {
                        Log.e("zylthinking", "nav_ioctl failed");
                    }
                }

                if (n == 0) {
                    aec_type = type;
                    speaker = 1;
                    routebutton.setText("外放");
                    if (aec_type == mmapi.aec_webrtc_mb) {
                        aecbutton.setText("mb");
                    } else {
                        aecbutton.setText("pc");
                    }
                }
            }
        });

        snapbutton.setOnClickListener(new OnClickListener() {
            public void onClick(View view) {
                TextureView bigger = (TextureView) findViewById(R.id.textureView);
                Bitmap bitmap = null;
                if (bigger != null) {
                    try {
                        bitmap = bigger.getBitmap();
                    } catch (Throwable localThrowable) {}
                }
                post_message("bitmap = " + bitmap + "\n");
            }
        });

        capbutton.setOnCheckedChangeListener(new OnCheckedChangeListener() {
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                if (isChecked) {
                    String file = "/sdcard/a.silk";
                    cap_handle = mmapi.mmv_captofile(file.getBytes(), mmapi.type_none, aec_type);
                    if (cap_handle == -1) {
                        capbutton.setChecked(false);
                    }
                } else if (cap_handle != -1) {
                    mmapi.mmv_capstop(cap_handle);
                    cap_handle = -1;
                }
                reset_button_stat();
            }
        });

        testbutton.setOnCheckedChangeListener(new OnCheckedChangeListener() {
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                if (isChecked) {
                    String file = "/sdcard/a.silk";
                    play_handle = mmapi.mmv_playfile(file.getBytes());
                    if (play_handle == -1) {
                        testbutton.setChecked(false);
                        return;
                    }

                    if (cap_handle == -1) {
                        file = "/sdcard/c.silk";
                        cap_handle = mmapi.mmv_captofile(file.getBytes(), mmapi.type_none, aec_type);
                        if (cap_handle == -1) {
                            mmapi.mmv_stoplay(play_handle);
                            play_handle = -1;
                            testbutton.setChecked(false);
                        }
                    }

                    speaker = 1;
                    routebutton.setText("外放");
                    switchMode(true);
                } else if (cap_handle != -1) {
                    mmapi.mmv_capstop(cap_handle);
                    cap_handle = -1;
                    mmapi.mmv_stoplay(play_handle);
                    play_handle = -1;
                }
                reset_button_stat();
            }
        });

        szbutton.setOnCheckedChangeListener(new OnCheckedChangeListener() {
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                if (isChecked) {
                    monitor1.set_size(218, 824);
                } else {
                    monitor1.set_size(-1, -1);
                }
            }
        });

        mp4button.setOnCheckedChangeListener(new OnCheckedChangeListener() {
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                if (isChecked) {
                    if (pfcc != 0) {
                        String file = "/sdcard/z.mp4";
                        mp4_handle = mp4.mp4_begin(file.getBytes(), peer, cid, pfcc);
                    }

                    if (mp4_handle == 0) {
                        mp4button.setChecked(false);
                    }
                } else if (mp4_handle != 0) {
                    mp4.mp4_end(mp4_handle);
                }
            }
        });

        handler = new my_handler(new WeakReference<mmv>(this));
    }

    private static class my_handler extends Handler {
        private WeakReference wref = null;
        public my_handler(WeakReference wref) {
            this.wref = wref;
        }

        public void handleMessage(Message msg) {
            String message = (String) msg.obj;
            mmv obj = (mmv) wref.get();
            if (obj != null) {
                obj.panel.setText(message);
            }
        }
    }

    private void post_message(String text) {
        Message message = Message.obtain();
        message.obj = text;
        handler.sendMessage(message);
    }

    private String explain(long[] events, int idx) {
        int stat = (int) events[idx];
        String type = "audio";

        switch (stat) {
            case mmapi.notify_login:
                return "login ok\n";
            case mmapi.notify_disconnected:
                return "disconnected\n";
            case mmapi.notify_peer_count:
                return String.format("peer count %d\n", events[idx + 1]);
            case mmapi.notify_peer_stat:
                return "uid " + events[idx + 1] + " stat " + events[idx + 2] + "\n";
            case mmapi.notify_peer_stream:
                peer = events[idx + 1];
                if (events[idx + 2] != 0) {
                    type = "video";
                    if (pfcc == 0) {
                        pfcc = events[idx + 3];
                    }
                }
                cid = events[idx + 4];
                return String.format("uid %d %s stat %d\n", uid, events[idx + 2] != 0 ? "video" : "audio", events[idx + 3]);

            case mmapi.notify_statistic:
                return String.format("uid %d %s recv %d, lost: %d\n", events[idx + 1], events[idx + 2] != 0 ? "video" : "audio", events[idx + 4], events[idx + 3]);
            case mmapi.notify_io_stat:
                return String.format("io out %d io in %d\n", events[idx + 1], events[idx + 2]);
        }
        return "illegal status " + stat + "\n";
    }

    public void track() {
        while (true) {
            String msg = "current user: " + uid + "\nnetwork status: ";
            long[] events = mmapi.nav_status();
            if (events == null) {
                post_message("tracker exit");
                break;
            }

            int n = events.length;
            for (int i = 1; i < n; i += mmapi.notify_longs) {
                msg += explain(events, i);
            }
            post_message(msg);
        }
    }
}
