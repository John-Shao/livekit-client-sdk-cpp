#include "livekit/video_frame.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "livekit/ffi_handle.h"
#include "lk_log.h"
#include "video_utils.h"

namespace livekit {
namespace {

// Compute total buffer size in bytes for (width, height, type).
std::size_t computeBufferSize(int width, int height, VideoBufferType type) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument(
        "VideoFrame: width and height must be positive");
  }

  const auto w = static_cast<std::size_t>(width);
  const auto h = static_cast<std::size_t>(height);
  switch (type) {
  case VideoBufferType::ARGB:
  case VideoBufferType::ABGR:
  case VideoBufferType::RGBA:
  case VideoBufferType::BGRA:
    // 4 bytes per pixel
    return w * h * 4;

  case VideoBufferType::RGB24:
    // 3 bytes per pixel
    return w * h * 3;

  case VideoBufferType::I444:
    // Y, U, V all full resolution
    return w * h * 3;

  case VideoBufferType::I420:
  case VideoBufferType::NV12:
  case VideoBufferType::I010: {
    // Y full, U and V subsampled 2x2
    const std::size_t chroma_w = (w + 1) / 2;
    const std::size_t chroma_h = (h + 1) / 2;
    if (type == VideoBufferType::I420) {
      // Y (1 byte) + U (1 byte) + V (1 byte)
      return w * h + chroma_w * chroma_h * 2;
    } else if (type == VideoBufferType::NV12) {
      // Y (1 byte), UV interleaved (2 bytes per chroma sample)
      return w * h + chroma_w * chroma_h * 2;
    } else { // I010, 16 bits per sample in memory
      // Y: 2 bytes per sample, U & V: 2 bytes per sample
      return w * h * 2 + chroma_w * chroma_h * 4;
    }
  }

  case VideoBufferType::I420A: {
    // Y full, U & V 2x2, plus alpha full res
    const std::size_t chroma_w = (w + 1) / 2;
    const std::size_t chroma_h = (h + 1) / 2;
    // Y + A are full resolution, U + V subsampled
    return w * h * 2 + chroma_w * chroma_h * 2;
  }

  case VideoBufferType::I422: {
    // Y full, U & V subsampled horizontally only
    const std::size_t chroma_w = (w + 1) / 2;
    return w * h + chroma_w * h * 2;
  }

  default:
    throw std::runtime_error("VideoFrame: unsupported VideoBufferType");
  }
}

// Compute plane layout for (base_ptr, width, height, type)
std::vector<VideoPlaneInfo>
computePlaneInfos(uintptr_t base, int width, int height, VideoBufferType type) {
  std::vector<VideoPlaneInfo> planes;
  if (!base || width <= 0 || height <= 0) {
    LK_LOG_WARN("VideoFrame: invalid planeInfos input (ptr={}, w={}, h={})",
                base, width, height);
    return planes;
  }
  const auto w = static_cast<uint32_t>(width);
  const auto h = static_cast<uint32_t>(height);
  auto pushPlane = [&](uintptr_t ptr, uint32_t stride, uint32_t size) {
    VideoPlaneInfo info;
    info.data_ptr = ptr;
    info.stride = stride;
    info.size = size;
    planes.push_back(info);
  };

  switch (type) {
  case VideoBufferType::ARGB:
  case VideoBufferType::ABGR:
  case VideoBufferType::RGBA:
  case VideoBufferType::BGRA: {
    const uint32_t stride = w * 4;
    const uint32_t size = stride * h;
    pushPlane(base, stride, size);
    break;
  }

  case VideoBufferType::RGB24: {
    const uint32_t stride = w * 3;
    const uint32_t size = stride * h;
    pushPlane(base, stride, size);
    break;
  }

  case VideoBufferType::I420: {
    const uint32_t chroma_w = (w + 1) / 2;
    const uint32_t chroma_h = (h + 1) / 2;

    // Y
    const uint32_t y_stride = w;
    const uint32_t y_size = w * h;
    uintptr_t y_ptr = base;
    pushPlane(y_ptr, y_stride, y_size);

    // U
    const uint32_t u_stride = chroma_w;
    const uint32_t u_size = chroma_w * chroma_h;
    uintptr_t u_ptr = y_ptr + y_size;
    pushPlane(u_ptr, u_stride, u_size);

    // V
    const uint32_t v_stride = chroma_w;
    const uint32_t v_size = chroma_w * chroma_h;
    uintptr_t v_ptr = u_ptr + u_size;
    pushPlane(v_ptr, v_stride, v_size);
    break;
  }

  case VideoBufferType::I420A: {
    const uint32_t chroma_w = (w + 1) / 2;
    const uint32_t chroma_h = (h + 1) / 2;

    // Y
    const uint32_t y_stride = w;
    const uint32_t y_size = w * h;
    uintptr_t y_ptr = base;
    pushPlane(y_ptr, y_stride, y_size);

    // U
    const uint32_t u_stride = chroma_w;
    const uint32_t u_size = chroma_w * chroma_h;
    uintptr_t u_ptr = y_ptr + y_size;
    pushPlane(u_ptr, u_stride, u_size);

    // V
    const uint32_t v_stride = chroma_w;
    const uint32_t v_size = chroma_w * chroma_h;
    uintptr_t v_ptr = u_ptr + u_size;
    pushPlane(v_ptr, v_stride, v_size);

    // A (full res)
    const uint32_t a_stride = w;
    const uint32_t a_size = w * h;
    uintptr_t a_ptr = v_ptr + v_size;
    pushPlane(a_ptr, a_stride, a_size);
    break;
  }

  case VideoBufferType::I422: {
    const uint32_t chroma_w = (w + 1) / 2;

    // Y
    const uint32_t y_stride = w;
    const uint32_t y_size = w * h;
    uintptr_t y_ptr = base;
    pushPlane(y_ptr, y_stride, y_size);

    // U
    const uint32_t u_stride = chroma_w;
    const uint32_t u_size = chroma_w * h;
    uintptr_t u_ptr = y_ptr + y_size;
    pushPlane(u_ptr, u_stride, u_size);

    // V
    const uint32_t v_stride = chroma_w;
    const uint32_t v_size = chroma_w * h;
    uintptr_t v_ptr = u_ptr + u_size;
    pushPlane(v_ptr, v_stride, v_size);
    break;
  }

  case VideoBufferType::I444: {
    // All planes full-res
    const uint32_t y_stride = w;
    const uint32_t y_size = w * h;
    uintptr_t y_ptr = base;
    pushPlane(y_ptr, y_stride, y_size);

    const uint32_t u_stride = w;
    const uint32_t u_size = w * h;
    uintptr_t u_ptr = y_ptr + y_size;
    pushPlane(u_ptr, u_stride, u_size);

    const uint32_t v_stride = w;
    const uint32_t v_size = w * h;
    uintptr_t v_ptr = u_ptr + u_size;
    pushPlane(v_ptr, v_stride, v_size);
    break;
  }

  case VideoBufferType::I010: {
    // 16-bit per sample
    const uint32_t chroma_w = (w + 1) / 2;
    const uint32_t chroma_h = (h + 1) / 2;

    // Y
    const uint32_t y_stride = w * 2;
    const uint32_t y_size = w * h * 2;
    uintptr_t y_ptr = base;
    pushPlane(y_ptr, y_stride, y_size);

    // U
    const uint32_t u_stride = chroma_w * 2;
    const uint32_t u_size = chroma_w * chroma_h * 2;
    uintptr_t u_ptr = y_ptr + y_size;
    pushPlane(u_ptr, u_stride, u_size);

    // V
    const uint32_t v_stride = chroma_w * 2;
    const uint32_t v_size = chroma_w * chroma_h * 2;
    uintptr_t v_ptr = u_ptr + u_size;
    pushPlane(v_ptr, v_stride, v_size);
    break;
  }

  case VideoBufferType::NV12: {
    const uint32_t chroma_w = (w + 1) / 2;
    const uint32_t chroma_h = (h + 1) / 2;

    // Y
    const uint32_t y_stride = w;
    const uint32_t y_size = w * h;
    uintptr_t y_ptr = base;
    pushPlane(y_ptr, y_stride, y_size);

    // UV interleaved
    const uint32_t uv_stride = chroma_w * 2;
    const uint32_t uv_size = chroma_w * chroma_h * 2;
    uintptr_t uv_ptr = y_ptr + y_size;
    pushPlane(uv_ptr, uv_stride, uv_size);
    break;
  }

  default:
    // Unknown or unsupported -> no planes
    break;
  }

  return planes;
}

} // namespace

// ----------------------------------------------------------------------------
// VideoFrame implementation
// ----------------------------------------------------------------------------

VideoFrame::VideoFrame()
    : width_{0}, height_{0}, type_{VideoBufferType::BGRA}, data_{} {}

VideoFrame::VideoFrame(int width, int height, VideoBufferType type,
                       std::vector<std::uint8_t> data)
    : width_(width), height_(height), type_(type), data_(std::move(data)) {
  const std::size_t expected = computeBufferSize(width_, height_, type_);
  if (data_.size() < expected) {
    throw std::invalid_argument("VideoFrame: provided data is too small for "
                                "the specified format and size");
  }
}

VideoFrame VideoFrame::create(int width, int height, VideoBufferType type) {
  const std::size_t size = computeBufferSize(width, height, type);
  std::vector<std::uint8_t> buffer(size, 0);
  return VideoFrame(width, height, type, std::move(buffer));
}

std::vector<VideoPlaneInfo> VideoFrame::planeInfos() const {
  if (data_.empty()) {
    return {};
  }

  uintptr_t base = reinterpret_cast<uintptr_t>(data_.data());
  return computePlaneInfos(base, width_, height_, type_);
}

VideoFrame VideoFrame::convert(VideoBufferType dst, bool flip_y) const {
  // Fast path: same format, no flip -> just clone the buffer.
  // We still return a *new* VideoFrame, never `*this`, so copy-ctor
  // being deleted is not a problem.
  if (dst == type_ && !flip_y) {
    LK_LOG_WARN("VideoFrame::convert: converting to the same format");
    // copy pixel data
    std::vector<std::uint8_t> buf = data_;
    return VideoFrame(width_, height_, type_, std::move(buf));
  }

  // General path: delegate to the FFI-based conversion helper.
  // This returns a brand new VideoFrame (move-constructed / elided).
  return convertViaFfi(*this, dst, flip_y);
}

VideoFrame VideoFrame::fromOwnedInfo(const proto::OwnedVideoBuffer &owned) {
  const auto &info = owned.info();
  const int width = static_cast<int>(info.width());
  const int height = static_cast<int>(info.height());
  const VideoBufferType type = fromProto(info.type());

  std::vector<std::uint8_t> buffer;

  if (info.components_size() > 0) {
    // Multi-plane (e.g. I420, NV12). Pack planes back-to-back into a tight
    // buffer (no per-row padding) so consumers can iterate frame.data()
    // assuming `stride == row_bytes` per plane — that's the convention
    // planeInfos() reports and what every existing renderer assumes.
    //
    // The FFI side may hand us stride-padded planes (e.g. when Phase 7.6.b's
    // NV12 fast path passes the source MppBuffer through as-is, MPP's
    // hor_stride is 16-aligned and exceeds visible width on simulcast
    // layers like 360x640 → 368x640). A previous version blindly memcpy'd
    // comp.size bytes, which preserved the padding and produced
    // diagonally-banded garble at non-aligned resolutions; row-by-row
    // stride-strip fixes that and matches the destination tight layout
    // computePlaneInfos already advertises.
    const std::size_t dst_total = computeBufferSize(width, height, type);
    buffer.resize(dst_total);
    const auto dst_planes = computePlaneInfos(
        reinterpret_cast<uintptr_t>(buffer.data()), width, height, type);
    if (dst_planes.size() !=
        static_cast<std::size_t>(info.components_size())) {
      throw std::runtime_error(
          "VideoFrame::fromOwnedInfo: plane count mismatch between FFI "
          "buffer info and computed tight layout");
    }

    for (int i = 0; i < info.components_size(); ++i) {
      const auto &comp = info.components(i);
      const auto &dst = dst_planes[i];
      const std::size_t src_stride = static_cast<std::size_t>(comp.stride());
      const std::size_t dst_stride = static_cast<std::size_t>(dst.stride);
      const auto src_ptr = reinterpret_cast<const std::uint8_t *>(
          static_cast<std::uintptr_t>(comp.data_ptr()));
      auto *dst_ptr = reinterpret_cast<std::uint8_t *>(dst.data_ptr);

      if (src_stride == dst_stride) {
        // No padding to strip — bulk memcpy of the full plane.
        std::memcpy(dst_ptr, src_ptr, dst.size);
      } else if (src_stride > dst_stride && src_stride > 0) {
        // FFI plane has per-row padding (e.g. MPP hor_stride > width).
        // Strip it as we copy: dst_stride bytes per row, advancing src
        // by src_stride. Row count comes from comp.size, which the FFI
        // side fills as src_stride * rows.
        const std::size_t rows = comp.size() / src_stride;
        for (std::size_t r = 0; r < rows; ++r) {
          std::memcpy(dst_ptr + r * dst_stride,
                      src_ptr + r * src_stride, dst_stride);
        }
      } else {
        // Source row narrower than destination tight row — unexpected;
        // fall back to a bulk memcpy bounded by what we actually have.
        const std::size_t copy = std::min<std::size_t>(comp.size(), dst.size);
        std::memcpy(dst_ptr, src_ptr, copy);
      }
    }
  } else {
    // Packed format: treat top-level data_ptr as a single contiguous buffer.
    const auto src_ptr = reinterpret_cast<const std::uint8_t *>(
        static_cast<std::uintptr_t>(info.data_ptr()));

    std::size_t total_size = 0;
    if (info.has_stride()) {
      // Use stride * height as total size (includes per-row padding if any).
      total_size = static_cast<std::size_t>(info.stride()) *
                   static_cast<std::size_t>(height);
    } else {
      // Use our generic buffer-size helper (width/height/type).
      total_size = computeBufferSize(width, height, type);
    }

    buffer.resize(total_size);
    std::memcpy(buffer.data(), src_ptr, total_size);
  }

  // Release the FFI-owned buffer after copying the data.
  {
    FfiHandle owned_handle(static_cast<std::uintptr_t>(owned.handle().id()));
    // owned_handle destroyed at end of scope → native buffer disposed.
  }

  return VideoFrame(width, height, type, std::move(buffer));
}

} // namespace livekit
