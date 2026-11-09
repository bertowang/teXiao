package com.texiao.demo;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.ImageFormat;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;

import java.nio.ByteBuffer;
import java.util.Arrays;

/**
 * Camera2 + ImageReader(YUV_420_888) 采集，整理成紧密的 Y / 交错 UV 两平面，
 * 走引擎已有的 fsNormalizeNV12（不需要 SurfaceTexture / GL_TEXTURE_EXTERNAL_OES）。
 * 与 iOS FXCameraController / macOS FXMacCameraController 处于同一层。
 */
public class FXCamera2 {
    private static final String TAG = "TeXiao";
    private static final int WIDTH = 1280;
    private static final int HEIGHT = 720;

    public interface FrameListener {
        void onFrame(byte[] y, byte[] uv, int width, int height, int rotationQuarterTurns,
                     boolean mirror);
    }

    private final Context context;
    private FrameListener listener;
    private HandlerThread thread;
    private Handler handler;
    private ImageReader reader;
    private CameraDevice camera;
    private CameraCaptureSession session;
    private int rotationQuarterTurns = 0;
    private volatile boolean running = false;

    public FXCamera2(Context context) { this.context = context.getApplicationContext(); }

    public void setListener(FrameListener value) { this.listener = value; }

    public boolean hasPermission() {
        return context.checkSelfPermission(Manifest.permission.CAMERA)
                == PackageManager.PERMISSION_GRANTED;
    }

    public boolean isRunning() { return running; }

    public synchronized boolean start() {
        if (running || !hasPermission()) return false;
        thread = new HandlerThread("texiao.camera");
        thread.start();
        handler = new Handler(thread.getLooper());
        reader = ImageReader.newInstance(WIDTH, HEIGHT, ImageFormat.YUV_420_888, 2);
        reader.setOnImageAvailableListener(this::onImageAvailable, handler);

        final CameraManager manager =
                (CameraManager) context.getSystemService(Context.CAMERA_SERVICE);
        if (manager == null) return false;
        try {
            String cameraId = null;
            for (String id : manager.getCameraIdList()) {
                CameraCharacteristics chars = manager.getCameraCharacteristics(id);
                Integer facing = chars.get(CameraCharacteristics.LENS_FACING);
                if (facing != null && facing == CameraCharacteristics.LENS_FACING_BACK) {
                    cameraId = id;
                    Integer sensor = chars.get(CameraCharacteristics.SENSOR_ORIENTATION);
                    rotationQuarterTurns = ((sensor == null ? 90 : sensor) / 90) % 4;
                    break;
                }
            }
            if (cameraId == null) return false;
            manager.openCamera(cameraId, stateCallback, handler);
            running = true;
            return true;
        } catch (CameraAccessException | SecurityException e) {
            Log.e(TAG, "openCamera failed: " + e.getMessage());
            return false;
        }
    }

    private final CameraDevice.StateCallback stateCallback = new CameraDevice.StateCallback() {
        @Override
        public void onOpened(CameraDevice device) {
            camera = device;
            try {
                final CaptureRequest.Builder builder =
                        device.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
                builder.addTarget(reader.getSurface());
                device.createCaptureSession(Arrays.asList(reader.getSurface()),
                        new CameraCaptureSession.StateCallback() {
                            @Override
                            public void onConfigured(CameraCaptureSession s) {
                                session = s;
                                try {
                                    s.setRepeatingRequest(builder.build(), null, handler);
                                } catch (CameraAccessException e) {
                                    Log.e(TAG, "repeating request failed: " + e.getMessage());
                                }
                            }

                            @Override
                            public void onConfigureFailed(CameraCaptureSession s) {
                                Log.e(TAG, "capture session configuration failed");
                            }
                        }, handler);
            } catch (CameraAccessException e) {
                Log.e(TAG, "createCaptureSession failed: " + e.getMessage());
            }
        }

        @Override
        public void onDisconnected(CameraDevice device) {
            device.close();
            camera = null;
        }

        @Override
        public void onError(CameraDevice device, int error) {
            Log.e(TAG, "camera error: " + error);
            device.close();
            camera = null;
        }
    };

    private void onImageAvailable(ImageReader imageReader) {
        Image image = null;
        try {
            image = imageReader.acquireLatestImage();
            if (image == null || !running) return;
            final int width = image.getWidth();
            final int height = image.getHeight();
            final Image.Plane[] planes = image.getPlanes();
            if (planes.length < 3) return;

            final ByteBuffer yBuffer = planes[0].getBuffer();
            final ByteBuffer uBuffer = planes[1].getBuffer();
            final ByteBuffer vBuffer = planes[2].getBuffer();
            final int yRow = planes[0].getRowStride();
            final int yPixel = planes[0].getPixelStride();
            final int uvRow = planes[1].getRowStride();
            final int uvPixel = planes[1].getPixelStride();
            final int vRow = planes[2].getRowStride();
            final int vPixel = planes[2].getPixelStride();
            final int cw = width / 2;
            final int ch = height / 2;

            final byte[] y = new byte[width * height];
            final byte[] uv = new byte[cw * ch * 2];
            for (int row = 0; row < height; ++row) {
                for (int col = 0; col < width; ++col) {
                    y[row * width + col] = yBuffer.get(row * yRow + col * yPixel);
                }
            }
            for (int row = 0; row < ch; ++row) {
                for (int col = 0; col < cw; ++col) {
                    final int index = row * uvRow + col * uvPixel;
                    final int out = (row * cw + col) * 2;
                    uv[out] = uBuffer.get(index);
                    uv[out + 1] = uvPixel >= 2
                            ? uBuffer.get(index + 1)
                            : vBuffer.get(row * vRow + col * vPixel);
                }
            }
            if (listener != null) {
                listener.onFrame(y, uv, width, height, rotationQuarterTurns, false);
            }
        } finally {
            if (image != null) image.close();
        }
    }

    public synchronized void stop() {
        running = false;
        if (session != null) { session.close(); session = null; }
        if (camera != null) { camera.close(); camera = null; }
        if (reader != null) { reader.close(); reader = null; }
        if (thread != null) { thread.quitSafely(); thread = null; }
        handler = null;
    }
}
