
package libmedia;
import java.lang.Thread;
import java.nio.ByteBuffer;
import android.util.Log;
import android.util.Range;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaCodecInfo.CodecCapabilities;
import android.media.MediaCodecInfo.CodecProfileLevel;
import android.media.MediaFormat;

class video_encoder extends Thread {
    private static final int csp_i420 = 3;
    private static final int csp_nv12 = 5;

    private long something = 0;
    private int width = 0;
    private int height = 0;
    private int csp = 0;
    private int gop = 0;
    private int fps = 0;
    private int bps = 0;
    private int omx_color = 0;
    private int profile = CodecProfileLevel.AVCProfileBaseline;
    private int level = CodecProfileLevel.AVCLevel1;
    private MediaCodec enc = null;
    private final String MIME = "video/avc";
    private final int max_point = 7;

    public video_encoder(long something,
        int width, int height, int csp, int gop, int fps, int kbps)
        throws Exception
    {
        this.something = something;
        this.width = width;
        this.height = height;
        this.csp = csp;
        this.gop = gop;
        this.fps = fps;
        this.bps = kbps * 1000;

        enc = create_encoder();
        if (enc == null) {
            throw new Exception("failed to create encoder");
        }
    }

    private String select_encoder() {
        int color = 0;
        int point = max_point;
        int i, j, nr = MediaCodecList.getCodecCount();
        MediaCodecInfo codec_info = null;

        for (i = 0; i < nr && point > 0; i++) {
            MediaCodecInfo info = MediaCodecList.getCodecInfoAt(i);
            if (!info.isEncoder()) {
                continue;
            }

            String[] types = info.getSupportedTypes();
            for (j = 0; j < types.length; j++) {
                if (!types[j].equalsIgnoreCase(MIME)) {
                    continue;
                }
                break;
            }

            if (j == types.length) {
                continue;
            }

            CodecCapabilities codec_capability = info.getCapabilitiesForType(MIME);
            if (codec_capability == null) {
                continue;
            }

            int n = codec_point(codec_capability);
            if (n >= point) {
                continue;
            }
            point = n;
            color = omx_color;
            codec_info = info;
        }

        if (codec_info == null) {
            return null;
        }

        omx_color = color;
        if (omx_color == CodecCapabilities.COLOR_FormatYUV420SemiPlanar) {
            csp = csp_nv12;
        } else if (omx_color == CodecCapabilities.COLOR_FormatYUV420Planar) {
            csp = csp_i420;
        } else if (omx_color == CodecCapabilities.COLOR_TI_FormatYUV420PackedSemiPlanar) {
            csp = csp_nv12;
        } else {
            Log.e("zylthinking", "no branch for omx color" + omx_color);
            return null;
        }
        return codec_info.getName();
    }

    private MediaCodec create_encoder() throws Exception {
        String name = select_encoder();
        if (name == null) {
            return null;
        }

        MediaCodec object = MediaCodec.createByCodecName(name);
        MediaFormat format = MediaFormat.createVideoFormat(MIME, width, height);
        format.setInteger(MediaFormat.KEY_BIT_RATE, bps);
        format.setInteger(MediaFormat.KEY_COLOR_FORMAT, omx_color);
        format.setInteger(MediaFormat.KEY_FRAME_RATE, fps);
        format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, (gop + fps - 1) / fps);
        try {
            format.setInteger(MediaFormat.KEY_PROFILE, profile);
            format.setInteger("level", level);
        } catch (Throwable exception) {}

        object.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
        try {
            object.start();
        } catch (Throwable exception) {
            object.release();
            object = null;
        }
        return object;
    }

    private int codec_point(CodecCapabilities codec_capability) {
        int point = max_point;
        int color = color_space_matched(codec_capability.colorFormats);
        if (color == 0) {
            return point;
        }
        omx_color = color;
        point -= 1;

        for (int i = 0; i < codec_capability.profileLevels.length; ++i) {
            CodecProfileLevel level = codec_capability.profileLevels[i];
            if (level.profile == CodecProfileLevel.AVCProfileHigh) {
                profile = level.profile;
                this.level = level.level;
                break;
            }

            if (level.profile == CodecProfileLevel.AVCProfileMain && profile == CodecProfileLevel.AVCProfileBaseline) {
                profile = level.profile;
                this.level = level.level;
            }
        }

        if (profile == CodecProfileLevel.AVCProfileHigh) {
            point -= 2;
        } else if (profile == CodecProfileLevel.AVCProfileMain) {
            point -= 1;
        }

        MediaCodecInfo.VideoCapabilities video_capability = null;
        try {
            video_capability = codec_capability.getVideoCapabilities();
        } catch (Throwable t) {
            // API 21- reach here
            return point;
        }

        if (!video_capability.isSizeSupported(width, height)) {
            Range<Integer> widths = video_capability.getSupportedWidths();
            Range<Integer> heights = video_capability.getSupportedHeights();
            int w_align = video_capability.getWidthAlignment();
            int h_align = video_capability.getHeightAlignment();
            Log.e("zylthinking", "width " + widths.getLower() + ":" + widths.getUpper() +
                  ", height " + heights.getLower() + ":" + heights.getUpper() + ", align " + w_align + ":" + h_align);
            return max_point;
        }

        Range<Double> fps_range = video_capability.getSupportedFrameRatesFor(width, height);
        int lower = fps_range.getLower().intValue() + 1;
        int upper = fps_range.getUpper().intValue();
        if (lower > upper) {
            lower = upper;
        }

        if (fps < lower) {
            fps = lower;
            point -= 1;
        }  else if (fps > upper) {
            fps = upper;
            point -= 1;
        } else {
            point -= 2;
        }

        Range<Integer> bps_range = video_capability.getBitrateRange();
        lower = bps_range.getLower();
        upper = bps_range.getUpper();
        if (bps < lower) {
            bps = lower;
            point -= 1;
        } else if (bps > upper) {
            bps = upper;
            point -= 1;
        } else {
            point -= 2;
        }
        return point;
    }

    private int color_space_matched(int[] colors) {
        for (int i = 0; i < colors.length; ++i) {
            int color = colors[i];
            if (color == CodecCapabilities.COLOR_FormatYUV420SemiPlanar ||
                color == CodecCapabilities.COLOR_FormatYUV420Planar ||
                color == CodecCapabilities.COLOR_TI_FormatYUV420PackedSemiPlanar)
            {
                return color;
            }
        }

        for (int i = 0; i < colors.length; ++i) {
            Log.e("zylthinking", "encoder accepted csp " + colors[i] + " not support by us");
            int color = colors[i];
        }
        return 0;
    }

    public void run() {
        int idx = 0, n = 0;
        long[] meta = {0, 1};
        ByteBuffer buffer = null;
        ByteBuffer[] input = enc.getInputBuffers();
        ByteBuffer[] output = enc.getOutputBuffers();
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();

        while (n != -1) {
            int idle = 1;
            byte[] yuv = codec.pixel_pull(something, width, height, csp, meta);
            if (meta[0] == -1) {
                break;
            }

            if (yuv != null) {
                idle = 0;
                try {
                    idx = enc.dequeueInputBuffer(1000 * 500);
                } catch (Throwable t) {
                    idx = -1;
                }

                if (idx >= 0) {
                    buffer = input[idx];
                    buffer.clear();
                    buffer.put(yuv);
                    enc.queueInputBuffer(idx, 0, yuv.length, meta[1] * 1000 + meta[0], 0);
                }
            }

            do {
                try {
                    idx = enc.dequeueOutputBuffer(info, 0);
                } catch (Throwable t) {
                    break;
                }

                if (idx == MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED) {
                    output = enc.getOutputBuffers();
                    break;
                }

                if (idx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    Log.e("zylthinking", "output format changed");
                    break;
                }

                if (idx < 0) {
                    break;
                }

                idle = 0;
                buffer = output[idx];

                byte[] h264 = new byte[info.size];
                try {
                    buffer.get(h264, info.offset, info.size);
                    n = codec.h264_push(something, info.presentationTimeUs % 1000, info.presentationTimeUs / 1000, h264);
                } catch (Throwable exception) {
                    Log.e("zylthinking", "galaxy nexus has this bug");
                }
                buffer.clear();
                enc.releaseOutputBuffer(idx, false);
            } while (n != -1);

            if (idle == 1) {
                try {
                    Thread.sleep(40);
                } catch (InterruptedException e) {}
            }
        }

        codec.handle_dereference(something);
        try {
            enc.signalEndOfInputStream();
        } catch (Throwable t) {}
        enc.stop();
        enc.release();
    }
}

class codec {
    private static int encoder_init(long something,
        int width, int height, int csp, int gop, int fps, int kbps)
    {
        int n = 0;
        try {
            new video_encoder(something, width, height, csp, gop, fps, kbps).start();
        } catch (Throwable t) {
            t.printStackTrace();
            n = -1;
        }
        return n;
    }

    static native void handle_dereference(long something);
    static native byte[] pixel_pull(long something, int width, int height, int csp, long[] meta);
    static native int h264_push(long something, long angle, long pts, byte[] h264);
    static {
        System.load(runtime.path + "libmedia2.so");
    }
}
