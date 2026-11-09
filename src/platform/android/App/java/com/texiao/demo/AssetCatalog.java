package com.texiao.demo;

import android.content.res.AssetManager;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** 与 macOS 的 FXAssetCatalog 等价：直接读 JSON 的 ui 元数据生成控件，不硬编码 pass / 参数。 */
public final class AssetCatalog {
    private static final String TAG = "TeXiao";

    public static final class Info {
        public String file;
        public String label;
        public String pass;
        public String param;
        public float min;
        public float max;
        public float value;

        @Override public String toString() { return label; }
    }

    private AssetCatalog() {}

    public static List<Info> load(AssetManager assets) {
        final List<Info> result = new ArrayList<>();
        try {
            String[] names = assets.list("");
            if (names == null) return result;
            Arrays.sort(names);
            for (String name : names) {
                if (!name.startsWith("demo_") || !name.endsWith(".json")) continue;
                JSONObject json = new JSONObject(readAll(assets.open(name)));
                JSONObject ui = json.optJSONObject("ui");
                if (ui == null) ui = new JSONObject();

                Info info = new Info();
                info.file = name;
                info.label = ui.optString("label", name.replace(".json", ""));
                info.pass = ui.optString("primaryPass", "");
                info.param = ui.optString("primaryParam", "");
                info.min = 0f;
                info.max = 1f;
                JSONArray range = ui.optJSONArray("primaryRange");
                if (range != null && range.length() == 2) {
                    info.min = (float) range.optDouble(0, 0.0);
                    info.max = (float) range.optDouble(1, 1.0);
                } else if ("radius".equals(info.param)) {
                    info.max = 8f;
                } else if ("scale".equals(info.param)) {
                    info.min = 0.05f;
                    info.max = 1.5f;
                }
                info.max = Math.max(info.min + 0.01f, info.max);
                info.value = (info.min + info.max) * 0.5f;

                JSONArray graph = json.optJSONArray("renderGraph");
                if (graph != null) {
                    for (int i = 0; i < graph.length(); ++i) {
                        JSONObject node = graph.optJSONObject(i);
                        if (node == null || !info.pass.equals(node.optString("id"))) continue;
                        JSONObject params = node.optJSONObject("params");
                        if (params != null && params.has(info.param)) {
                            info.value = (float) params.optDouble(info.param, info.value);
                        }
                        break;
                    }
                }
                info.value = Math.max(info.min, Math.min(info.max, info.value));
                result.add(info);
            }
        } catch (Exception e) {
            Log.e(TAG, "asset catalog failed: " + e.getMessage());
        }
        return result;
    }

    private static String readAll(InputStream input) throws Exception {
        try (InputStream in = input; ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            int read;
            while ((read = in.read(buffer)) > 0) out.write(buffer, 0, read);
            return new String(out.toByteArray(), StandardCharsets.UTF_8);
        }
    }
}
