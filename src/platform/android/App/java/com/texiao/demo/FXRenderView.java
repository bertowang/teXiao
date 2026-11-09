package com.texiao.demo;

import android.content.Context;
import android.content.res.AssetManager;
import android.opengl.GLSurfaceView;
import android.os.Handler;
import android.os.Looper;
import android.util.AttributeSet;
import android.util.Log;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * 与 iOS 的 FXPreviewView / macOS 的 FXMetalView 对应。
 * GLSurfaceView 负责 EGL context 与 swap；本类把 UI 线程指令用 queueEvent 投递到
 * 唯一的 GLThread，满足引擎「所有 GPU 调用收敛到一条串行渲染队列」的契约。
 */
public class FXRenderView extends GLSurfaceView implements GLSurfaceView.Renderer {
    private static final String TAG = "TeXiao";

    public static final int MODE_TEST = 0;
    public static final int MODE_CAMERA = 1;
    public static final int MODE_IMAGE = 2;

    public interface StatsListener { void onStats(String json); }

    private final FXEngine engine = new FXEngine();
    private final AssetManager assets;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Object frameLock = new Object();

    private StatsListener statsListener;
    private volatile int mode = MODE_TEST;
    private volatile boolean paused = false;
    private long startTimeNs = 0;
    private long frameIndex = 0;
    private int tick = 0;

    // 相机帧与图片帧：JNI 在 RenderFrame 期间读取，因此用同一把锁保护。
    private byte[] cameraY, cameraUv, imageRgba;
    private int cameraW, cameraH, cameraRotation, imageW, imageH;
    private boolean cameraMirror, hasCameraFrame, hasImage;

    public FXRenderView(Context context, AttributeSet attrs) {
        super(context, attrs);
        this.assets = context.getAssets();
        setEGLContextClientVersion(3);
        setEGLConfigChooser(8, 8, 8, 8, 0, 0);
        setRenderer(this);
        setRenderMode(RENDERMODE_CONTINUOUSLY);
    }

    public void setStatsListener(StatsListener listener) { this.statsListener = listener; }

    // ---- Renderer（全部运行在 GLThread）----
    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        startTimeNs = System.nanoTime();
        if (engine.create(assets) == 0) Log.e(TAG, "engine creation failed");
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        engine.surfaceChanged(width, height);
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        if (!engine.isReady()) return;
        final long timeUs = (System.nanoTime() - startTimeNs) / 1000L;
        final long index = ++frameIndex;
        synchronized (frameLock) {
            if (paused) return;
            if (mode == MODE_CAMERA && hasCameraFrame) {
                engine.renderNV12(cameraY, cameraUv, cameraW, cameraH, cameraRotation,
                        cameraMirror, timeUs, index);
            } else if (mode == MODE_IMAGE && hasImage) {
                engine.renderRGBA(imageRgba, imageW, imageH, timeUs, index);
            } else {
                engine.renderTestPattern(720, 1280, timeUs, index);
            }
        }
        if (++tick % 10 == 0 && statsListener != null) {
            final String json = engine.stats();
            mainHandler.post(() -> {
                if (statsListener != null) statsListener.onStats(json);
            });
        }
    }

    // ---- UI 线程 API ----
    public void loadAsset(final String name) {
        queueEvent(() -> {
            if (!engine.loadAsset(name)) Log.e(TAG, "asset load failed: " + name);
        });
    }

    public void setParam(final String pass, final String key, final float value) {
        queueEvent(() -> engine.setParam(pass, key, value));
    }

    public void setMode(int value) { this.mode = value; }

    public void submitCameraFrame(byte[] y, byte[] uv, int width, int height, int rotation,
                                  boolean mirror) {
        synchronized (frameLock) {
            cameraY = y;
            cameraUv = uv;
            cameraW = width;
            cameraH = height;
            cameraRotation = rotation;
            cameraMirror = mirror;
            hasCameraFrame = (y != null && uv != null);
        }
    }

    public void submitImage(byte[] rgba, int width, int height) {
        synchronized (frameLock) {
            imageRgba = rgba;
            imageW = width;
            imageH = height;
            hasImage = (rgba != null);
        }
    }

    public void requestReadback(final java.util.function.LongConsumer callback) {
        queueEvent(() -> {
            final long checksum = engine.readPixelChecksum();
            mainHandler.post(() -> callback.accept(checksum));
        });
    }

    public void pauseEngine() {
        paused = true;
        queueEvent(engine::onPause);
    }

    public void resumeEngine() {
        paused = false;
        queueEvent(engine::onResume);
    }

    /** 与 Activity.onDestroy 配对，必须在 GLThread 上释放引擎。 */
    public void releaseEngine() {
        queueEvent(() -> {
            engine.detachSurface();
            engine.destroy();
        });
    }
}
