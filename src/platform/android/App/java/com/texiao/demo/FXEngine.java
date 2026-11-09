package com.texiao.demo;

import android.content.res.AssetManager;

/** JNI 门面：与 iOS 的 FXEngineFacade / macOS 的 FXEngineFacade 同层。 */
public final class FXEngine {
    static {
        System.loadLibrary("fxjni");
    }

    private long handle = 0;

    /** 必须在 GLSurfaceView 的 GLThread 上调用（GLES3 context 已 current）。 */
    public long create(AssetManager assets) {
        handle = nativeCreate(assets);
        return handle;
    }

    public void destroy() {
        if (handle != 0) {
            nativeDestroy(handle);
            handle = 0;
        }
    }

    public boolean isReady() { return handle != 0; }

    public void surfaceChanged(int width, int height) {
        if (handle != 0) nativeSurfaceChanged(handle, width, height);
    }

    public boolean loadAsset(String name) {
        return handle != 0 && nativeLoadAsset(handle, name);
    }

    public void setParam(String pass, String key, float value) {
        if (handle != 0) nativeSetParam(handle, pass, key, value);
    }

    public boolean renderTestPattern(int width, int height, long timeUs, long frameIndex) {
        return handle != 0 && nativeRenderTestPattern(handle, width, height, timeUs, frameIndex);
    }

    public boolean renderNV12(byte[] y, byte[] uv, int width, int height, int rotation,
                              boolean mirror, long timeUs, long frameIndex) {
        return handle != 0 && nativeRenderNV12(handle, y, uv, width, height, rotation,
                mirror, timeUs, frameIndex);
    }

    public boolean renderRGBA(byte[] rgba, int width, int height, long timeUs, long frameIndex) {
        return handle != 0 && nativeRenderRGBA(handle, rgba, width, height, timeUs, frameIndex);
    }

    public long readPixelChecksum() {
        return handle == 0 ? 0 : nativeReadPixelChecksum(handle);
    }

    public String stats() {
        return handle == 0 ? "{}" : nativeStats(handle);
    }

    public void onPause() { if (handle != 0) nativeOnPause(handle); }
    public void onResume() { if (handle != 0) nativeOnResume(handle); }
    public void detachSurface() { if (handle != 0) nativeDetachSurface(handle); }

    private static native long nativeCreate(AssetManager assets);
    private static native void nativeDestroy(long handle);
    private static native void nativeSurfaceChanged(long handle, int width, int height);
    private static native boolean nativeLoadAsset(long handle, String name);
    private static native void nativeSetParam(long handle, String pass, String key, float value);
    private static native boolean nativeRenderTestPattern(long handle, int width, int height,
                                                          long timeUs, long frameIndex);
    private static native boolean nativeRenderNV12(long handle, byte[] y, byte[] uv,
                                                   int width, int height, int rotation,
                                                   boolean mirror, long timeUs, long frameIndex);
    private static native boolean nativeRenderRGBA(long handle, byte[] rgba, int width,
                                                   int height, long timeUs, long frameIndex);
    private static native long nativeReadPixelChecksum(long handle);
    private static native String nativeStats(long handle);
    private static native void nativeOnPause(long handle);
    private static native void nativeOnResume(long handle);
    private static native void nativeDetachSurface(long handle);
}
