package com.texiao.demo;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Bundle;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONObject;

import java.util.List;

/** 与 iOS ViewController / macOS FXMacViewController 对应的宿主界面。 */
public class MainActivity extends Activity {
    private static final int REQ_CAMERA = 1001;
    private static final int REQ_IMAGE = 1002;

    private FXRenderView renderView;
    private FXCamera2 camera;
    private List<AssetCatalog.Info> assets;
    private int selected = 0;

    private TextView hud;
    private TextView toast;
    private TextView paramLabel;
    private SeekBar paramSeek;
    private SeekBar brightSeek;
    private Button cameraButton;
    private Button pauseButton;
    private Spinner assetSpinner;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        renderView = findViewById(R.id.preview);
        hud = findViewById(R.id.hud);
        toast = findViewById(R.id.toast);
        paramLabel = findViewById(R.id.paramLabel);
        paramSeek = findViewById(R.id.paramSeek);
        brightSeek = findViewById(R.id.brightnessSeek);
        assetSpinner = findViewById(R.id.assetSpinner);
        cameraButton = findViewById(R.id.cameraButton);
        pauseButton = findViewById(R.id.pauseButton);

        assets = AssetCatalog.load(getAssets());
        if (assets.isEmpty()) {
            showToast("未找到 assets/*.json 素材，请检查 Gradle assets.srcDirs");
        } else {
            assetSpinner.setAdapter(new ArrayAdapter<>(this,
                    android.R.layout.simple_spinner_dropdown_item, assets));
            selected = Math.min(1, assets.size() - 1);
            assetSpinner.setSelection(selected);
            assetSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                @Override public void onItemSelected(AdapterView<?> parent, View view,
                                                    int position, long id) {
                    selected = position;
                    applyAsset();
                }
                @Override public void onNothingSelected(AdapterView<?> parent) {}
            });
            applyAsset();
        }

        paramSeek.setOnSeekBarChangeListener(onSeek(() -> {
            AssetCatalog.Info info = assets.get(selected);
            if (!info.pass.isEmpty() && !info.param.isEmpty()) {
                renderView.setParam(info.pass, info.param, scaled(paramSeek));
                paramLabel.setText(info.pass + "." + info.param + " = " + scaled(paramSeek));
            }
        }));
        brightSeek.setOnSeekBarChangeListener(onSeek(() ->
                renderView.setParam("adjust", "brightness",
                        (brightSeek.getProgress() / 100f) - 0.5f)));

        findViewById(R.id.patternButton).setOnClickListener(v -> {
            if (camera != null) camera.stop();
            cameraButton.setText("摄像头");
            renderView.setMode(FXRenderView.MODE_TEST);
            showToast("输入: 程序化测试图");
        });
        cameraButton.setOnClickListener(v -> toggleCamera());
        findViewById(R.id.imageButton).setOnClickListener(v ->
                startActivityForResult(new Intent(Intent.ACTION_GET_CONTENT)
                        .setType("image/*"), REQ_IMAGE));
        pauseButton.setOnClickListener(v -> togglePause());
        findViewById(R.id.readbackButton).setOnClickListener(v ->
                renderView.requestReadback(value ->
                        showToast(String.format("CPU 回读 checksum: %016x", value))));

        camera = new FXCamera2(this);
        camera.setListener((y, uv, w, h, rotation, mirror) ->
                renderView.submitCameraFrame(y, uv, w, h, rotation, mirror));

        renderView.setStatsListener(this::updateHud);
    }

    private void applyAsset() {
        if (assets.isEmpty()) return;
        final AssetCatalog.Info info = assets.get(selected);
        renderView.loadAsset(info.file);
        paramSeek.setEnabled(!info.pass.isEmpty());
        final int steps = 1000;
        paramSeek.setMax(steps);
        paramSeek.setProgress((int) ((info.value - info.min) / (info.max - info.min) * steps));
        paramLabel.setText(info.pass.isEmpty()
                ? "—  ·  下方滑块: 亮度"
                : info.pass + "." + info.param + " = " + info.value);
        showToast("素材: " + info.label + "（JSON 驱动）");
    }

    private float scaled(SeekBar bar) {
        final AssetCatalog.Info info = assets.get(selected);
        return info.min + (info.max - info.min) * (bar.getProgress() / 1000f);
    }

    private SeekBar.OnSeekBarChangeListener onSeek(Runnable action) {
        return new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int p, boolean fromUser) {
                if (fromUser) action.run();
            }
            @Override public void onStartTrackingTouch(SeekBar s) {}
            @Override public void onStopTrackingTouch(SeekBar s) {}
        };
    }

    private void toggleCamera() {
        if (camera.hasPermission() && renderViewIsCamera()) {
            camera.stop();
            cameraButton.setText("摄像头");
            renderView.setMode(FXRenderView.MODE_TEST);
            showToast("已切回程序化测试图");
            return;
        }
        if (!camera.hasPermission()) {
            requestPermissions(new String[]{Manifest.permission.CAMERA}, REQ_CAMERA);
            return;
        }
        if (camera.start()) {
            renderView.setMode(FXRenderView.MODE_CAMERA);
            cameraButton.setText("关闭摄像头");
            showToast("摄像头已开启（YUV_420_888 → NormalizePass）");
        } else {
            showToast("没有可用摄像头 → 继续使用测试图");
        }
    }

    private boolean renderViewIsCamera() { return camera.isRunning(); }

    private void togglePause() {
        final boolean willPause = pauseButton.getText().toString().equals("暂停");
        pauseButton.setText(willPause ? "继续" : "暂停");
        if (willPause) renderView.pauseEngine();
        else renderView.resumeEngine();
        showToast(willPause ? "已暂停：瞬态 FBO 释放，generation 递增" : "已恢复渲染");
    }

    private void updateHud(String json) {
        try {
            JSONObject s = new JSONObject(json);
            hud.setText("TeXiao · 跨平台实时特效引擎 · Android / GLES3\n"
                    + "fps " + s.optString("fps") + "   CPU " + s.optString("cpuMs") + " ms"
                    + "   frame #" + s.optString("frameIndex") + "\n"
                    + "处理 " + s.optString("width") + "x" + s.optString("height")
                    + "   Pass " + s.optString("passes")
                    + "   槽 " + s.optString("slots")
                    + "   FBO " + s.optString("fbo") + "\n"
                    + "降级 " + s.optString("degraded")
                    + "   代际 " + s.optString("generation")
                    + "   上屏 " + s.optString("presented") + "\n"
                    + "单一帧入口 RenderFrame(t, frame)");
        } catch (Exception ignored) {
            hud.setText("引擎尚未就绪");
        }
    }

    private void showToast(String text) {
        toast.setText(text);
        toast.setVisibility(View.VISIBLE);
        toast.removeCallbacks(hideToast);
        toast.postDelayed(hideToast, 2500);
    }

    private final Runnable hideToast = () -> toast.setVisibility(View.GONE);

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                           int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode == REQ_CAMERA) {
            if (results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) {
                toggleCamera();
            } else {
                showToast("相机权限被拒绝 → 使用程序化测试图");
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_IMAGE || resultCode != RESULT_OK || data == null
                || data.getData() == null) return;
        try {
            Bitmap bitmap = BitmapFactory.decodeStream(
                    getContentResolver().openInputStream(data.getData()));
            if (bitmap == null) return;
            final int maxEdge = 1024;
            if (Math.max(bitmap.getWidth(), bitmap.getHeight()) > maxEdge) {
                final float scale = maxEdge / (float) Math.max(bitmap.getWidth(),
                        bitmap.getHeight());
                bitmap = Bitmap.createScaledBitmap(bitmap,
                        Math.round(bitmap.getWidth() * scale),
                        Math.round(bitmap.getHeight() * scale), true);
            }
            final int w = bitmap.getWidth(), h = bitmap.getHeight();
            final int[] pixels = new int[w * h];
            bitmap.getPixels(pixels, 0, w, 0, 0, w, h);
            final byte[] rgba = new byte[w * h * 4];
            for (int i = 0; i < pixels.length; ++i) {
                final int c = pixels[i];
                rgba[i * 4] = (byte) ((c >> 16) & 0xFF);
                rgba[i * 4 + 1] = (byte) ((c >> 8) & 0xFF);
                rgba[i * 4 + 2] = (byte) (c & 0xFF);
                rgba[i * 4 + 3] = (byte) ((c >> 24) & 0xFF);
            }
            if (camera != null) camera.stop();
            cameraButton.setText("摄像头");
            renderView.submitImage(rgba, w, h);
            renderView.setMode(FXRenderView.MODE_IMAGE);
            showToast("图片输入 " + w + "x" + h + "（RGBA8）");
        } catch (Exception e) {
            Toast.makeText(this, "图片解码失败", Toast.LENGTH_SHORT).show();
        }
    }

    @Override protected void onResume() {
        super.onResume();
        renderView.onResume();
        renderView.resumeEngine();
    }

    @Override protected void onPause() {
        renderView.pauseEngine();
        if (camera != null) camera.stop();
        renderView.onPause();
        super.onPause();
    }

    @Override protected void onDestroy() {
        if (camera != null) camera.stop();
        renderView.releaseEngine();
        super.onDestroy();
    }
}
