#include "render/resource_manager.h"

#include "core/asset.h"
#include "core/log.h"
#include "render/procedural_textures.h"

namespace fx {

Status ResourceManager::Create(const AssetRuntime& runtime, IGraphicsDevice& gfx,
                               IPlatform* platform, std::vector<TextureHandle>& out) {
  DestroyAll(gfx);
  out.assign(runtime.resources.size(), kInvalidHandle);

  for (const ResourceInfo& info : runtime.resources) {
    const AssetResource& r = info.desc;
    uint32_t w = r.width, h = r.height;
    std::vector<uint8_t> pixels;

    if (r.type == "lut2d") {
      pixels = MakeLutStrip(r.generator, w, h);
    } else if (r.type == "rgba") {
      pixels = MakeRgbaImage(r.generator, w, h);
    } else if (r.type == "raw") {
      if (platform) pixels = platform->ReadFile(platform->ResolvePath(r.path));
      w = r.width;
      h = r.height;
      if (pixels.size() < static_cast<size_t>(w) * h * 4) {
        log::Warn("resource '" + r.name + "' raw data missing, using transparent fallback");
        pixels.assign(static_cast<size_t>(w) * h * 4, 0);
      }
    }

    if (pixels.empty()) {
      log::Warn("resource '" + r.name + "' has no usable data, degraded to identity");
      out[info.index] = kInvalidHandle;
      continue;
    }

    TextureDesc desc;
    desc.width = w;
    desc.height = h;
    desc.format = PixelFormat::RGBA8;
    desc.data = pixels.data();
    desc.dataSize = pixels.size();
    desc.bytesPerRow = w * 4;
    TextureHandle handle = gfx.CreateTexture(desc);
    out[info.index] = handle;
    if (handle != kInvalidHandle) owned_.push_back(handle);
  }
  return Status::Success();
}

void ResourceManager::DestroyAll(IGraphicsDevice& gfx) {
  for (TextureHandle h : owned_) gfx.DestroyTexture(h);
  owned_.clear();
}

}  // namespace fx
