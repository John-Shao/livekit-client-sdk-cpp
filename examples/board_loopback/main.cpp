/*
 * Single-process bidirectional smoke test for ATK-DLRV1126B board.
 *
 * - Publishes a synthetic RGBA gradient video track ("board-cam") and a data
 *   track ("board-data") so the remote peer (e.g. mobile LiveKit test app)
 *   can verify board → peer media flow.
 * - Implements RoomDelegate::onTrackSubscribed to register frame callbacks
 *   for EVERY remote track, regardless of identity or track name. Logs a
 *   per-stream counter every 5s so you can verify peer → board media flow.
 * - Auto-exits after 30 seconds — matches plan.md Phase 5 Go standard.
 *
 * Usage:
 *   BoardLoopback <ws-url> <token>
 *   LIVEKIT_URL=wss://... LIVEKIT_TOKEN=eyJ... BoardLoopback
 *
 * Token must grant `roomJoin` for the target room. The board's identity is
 * whatever the token's `sub` claim says.
 */

#include "livekit/audio_frame.h"
#include "livekit/audio_source.h"
#include "livekit/livekit.h"
#include "livekit/local_audio_track.h"
#include "livekit/local_data_track.h"
#include "livekit/local_participant.h"
#include "livekit/local_video_track.h"
#include "livekit/participant.h"
#include "livekit/room.h"
#include "livekit/room_delegate.h"
#include "livekit/room_event_types.h"
#include "livekit/track.h"
#include "livekit/video_frame.h"
#include "livekit/video_source.h"

#include <cmath>
#include <vector>

#include <alsa/asoundlib.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/videodev2.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_fourcc.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace livekit;

constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr const char *kVideoTrackName = "board-cam";
constexpr const char *kAudioTrackName = "board-mic";
constexpr int kAudioSampleRate = 48000;
constexpr int kAudioChannels = 1;
constexpr int kAudioFrameMs = 10; // SDK direct-capture 要求 10ms 帧 (= 480 @48k)
constexpr int kAudioSamplesPerChannel = kAudioSampleRate * kAudioFrameMs / 1000;
constexpr double kAudioToneHz = 440.0;     // A4 note，可听见的低频
constexpr double kAudioAmplitude = 6000.0; // ~ -15 dBFS, 不刺耳
constexpr int kReportEverySeconds = 5;

static std::atomic<bool> g_running{true};
static void handleSignal(int) { g_running.store(false); }

static std::string getenvOrEmpty(const char *name) {
  const char *v = std::getenv(name);
  return v ? std::string(v) : std::string{};
}

// Per-(identity, track) counters
struct LoopbackStats {
  std::atomic<std::uint64_t> video_frames{0};
  std::atomic<std::uint64_t> audio_frames{0};
};

struct StreamKey {
  std::string identity;
  std::string track;
  bool operator<(const StreamKey &o) const {
    if (identity != o.identity)
      return identity < o.identity;
    return track < o.track;
  }
};

class StatsRegistry {
public:
  std::shared_ptr<LoopbackStats> get(const std::string &id,
                                   const std::string &tr) {
    std::lock_guard<std::mutex> lk(mu_);
    auto &s = map_[StreamKey{id, tr}];
    if (!s)
      s = std::make_shared<LoopbackStats>();
    return s;
  }
  void print(std::ostream &os) {
    std::lock_guard<std::mutex> lk(mu_);
    if (map_.empty()) {
      os << "  (no remote streams yet)\n";
      return;
    }
    for (const auto &[k, s] : map_) {
      os << "  " << k.identity << ":" << k.track
         << " video=" << s->video_frames.load()
         << " audio=" << s->audio_frames.load() << "\n";
    }
  }

private:
  std::mutex mu_;
  std::map<StreamKey, std::shared_ptr<LoopbackStats>> map_;
};

static StatsRegistry g_stats;

// ---------------------------------------------------------------
// ALSA player: lazy-init PCM playback to plughw:0,0 (ES8389 codec on
// ATK-DLRV1126B). Producer-consumer split — SDK audio callback only
// enqueues into the FIFO and returns immediately; a dedicated writer
// thread drains the FIFO into snd_pcm_writei. This decouples the
// SDK's subscription thread from the codec's hardware pacing and
// avoids back-pressure-induced underruns.
// ---------------------------------------------------------------
class AlsaPlayer {
public:
  // Init from first audio frame's params and start the writer thread.
  bool ensureOpen(int sample_rate, int num_channels) {
    std::lock_guard<std::mutex> lk(open_mu_);
    if (pcm_) {
      if (sample_rate != sr_ || num_channels != ch_) {
        std::cerr << "[loopback] alsa: ignoring mismatched frame format\n";
        return false;
      }
      return true;
    }
    // 板上跑着 PulseAudio (PID 658) 占用 plughw 直通会跟它抢硬件，质量差。
    // 走 ALSA "default" 设备，PulseAudio 接管混音/重采样，质量更稳。
    // 允许通过 ALSA_PCM_DEVICE 环境变量覆盖（比如想 bypass pulse 时设
    // ALSA_PCM_DEVICE=plughw:0,0 直通）。
    const char *dev_env = std::getenv("ALSA_PCM_DEVICE");
    const char *device = (dev_env && *dev_env) ? dev_env : "default";
    snd_pcm_t *pcm = nullptr;
    int err = snd_pcm_open(&pcm, device, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
      std::cerr << "[loopback] alsa snd_pcm_open(" << device << ") failed: "
                << snd_strerror(err) << "\n";
      return false;
    }
    err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED, num_channels,
                             sample_rate, /*soft_resample=*/1,
                             /*latency_us=*/1000000); // 1s 缓冲，留足 jitter 余量
    if (err < 0) {
      std::cerr << "[loopback] alsa snd_pcm_set_params failed: "
                << snd_strerror(err) << "\n";
      snd_pcm_close(pcm);
      return false;
    }
    pcm_ = pcm;
    sr_ = sample_rate;
    ch_ = num_channels;
    std::cout << "[loopback] alsa playback opened: " << device << " "
              << sample_rate << "Hz " << num_channels << "ch s16le 1s-buf\n";
    writer_ = std::thread(&AlsaPlayer::writerLoop, this);
    return true;
  }

  // Called from SDK subscription thread — must be cheap (no PCM blocking).
  void enqueue(const std::int16_t *samples, std::size_t samples_per_channel) {
    if (!pcm_)
      return; // not opened yet (first-frame ensureOpen path)
    {
      std::lock_guard<std::mutex> lk(q_mu_);
      // Drop oldest if FIFO grows beyond ~500ms to bound memory in case the
      // codec is slower than producer (shouldn't happen at 48kHz mono but
      // safety net). 500ms @ 48k mono int16 = 48KB/frame*50frames ≈ 2.4MB.
      if (queue_.size() > 50) {
        queue_.pop_front();
        dropped_.fetch_add(1, std::memory_order_relaxed);
      }
      queue_.emplace_back(samples, samples + samples_per_channel);
    }
    q_cv_.notify_one();
  }

  std::uint64_t underruns() const {
    return underruns_.load(std::memory_order_relaxed);
  }
  std::uint64_t queue_dropped() const {
    return dropped_.load(std::memory_order_relaxed);
  }

  ~AlsaPlayer() {
    {
      std::lock_guard<std::mutex> lk(q_mu_);
      stop_ = true;
    }
    q_cv_.notify_all();
    if (writer_.joinable())
      writer_.join();
    if (pcm_) {
      snd_pcm_drain(pcm_);
      snd_pcm_close(pcm_);
      pcm_ = nullptr;
    }
  }

private:
  void writerLoop() {
    std::vector<std::int16_t> frame;
    while (true) {
      {
        std::unique_lock<std::mutex> lk(q_mu_);
        q_cv_.wait(lk, [this]() { return stop_ || !queue_.empty(); });
        if (stop_ && queue_.empty())
          return;
        frame = std::move(queue_.front());
        queue_.pop_front();
      }
      if (!pcm_ || frame.empty())
        continue;
      const std::size_t spc = frame.size() / ch_;
      snd_pcm_sframes_t written =
          snd_pcm_writei(pcm_, frame.data(), spc);
      if (written < 0) {
        const int err = static_cast<int>(written);
        if (err == -EPIPE)
          underruns_.fetch_add(1, std::memory_order_relaxed);
        snd_pcm_recover(pcm_, err, /*silent=*/1);
      }
    }
  }

  std::mutex open_mu_;
  snd_pcm_t *pcm_ = nullptr;
  int sr_ = 0;
  int ch_ = 0;

  std::mutex q_mu_;
  std::condition_variable q_cv_;
  std::deque<std::vector<std::int16_t>> queue_;
  bool stop_ = false;
  std::thread writer_;

  std::atomic<std::uint64_t> underruns_{0};
  std::atomic<std::uint64_t> dropped_{0};
};

static AlsaPlayer g_alsa;

// ---------------------------------------------------------------
// V4L2 multi-planar capture for /dev/video-camera0 (rkisp_mainpath
// on RV1126B). Negotiates NV12 at the requested resolution, mmaps
// 4 buffers, streams in blocking-poll mode. dequeue() returns the
// next NV12 frame as a freshly allocated vector ready to hand to
// VideoFrame.
// ---------------------------------------------------------------
class V4l2Capture {
public:
  bool open(const char *device, int width, int height) {
    fd_ = ::open(device, O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
      std::cerr << "[loopback] v4l2 open(" << device
                << ") failed: " << std::strerror(errno) << "\n";
      return false;
    }
    width_ = width;
    height_ = height;

    v4l2_capability cap{};
    if (ioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
      std::cerr << "[loopback] v4l2 QUERYCAP failed: "
                << std::strerror(errno) << "\n";
      return false;
    }
    if (!(cap.capabilities &
          (V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_DEVICE_CAPS))) {
      std::cerr << "[loopback] v4l2 device not multi-planar capture\n";
      return false;
    }

    // Set format: width × height NV12, single plane (NV12 is conceptually
    // 1 plane of (Y + interleaved UV) bytes).
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = width;
    fmt.fmt.pix_mp.height = height;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes = 1;
    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
      std::cerr << "[loopback] v4l2 S_FMT failed: " << std::strerror(errno)
                << "\n";
      return false;
    }
    width_ = fmt.fmt.pix_mp.width;
    height_ = fmt.fmt.pix_mp.height;
    plane_size_ = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
    std::cout << "[loopback] v4l2 negotiated: " << width_ << "x" << height_
              << " NV12 plane_size=" << plane_size_ << "\n";
    return true;
  }

  bool startStream(int buf_count = 4) {
    v4l2_requestbuffers reqbuf{};
    reqbuf.count = buf_count;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd_, VIDIOC_REQBUFS, &reqbuf) < 0) {
      std::cerr << "[loopback] v4l2 REQBUFS failed: "
                << std::strerror(errno) << "\n";
      return false;
    }

    buffers_.resize(reqbuf.count);
    for (std::uint32_t i = 0; i < reqbuf.count; ++i) {
      v4l2_buffer buf{};
      v4l2_plane planes[1]{};
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.index = i;
      buf.length = 1;
      buf.m.planes = planes;
      if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
        std::cerr << "[loopback] v4l2 QUERYBUF " << i
                  << " failed: " << std::strerror(errno) << "\n";
        return false;
      }
      buffers_[i].length = planes[0].length;
      buffers_[i].start =
          mmap(nullptr, planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED,
               fd_, planes[0].m.mem_offset);
      if (buffers_[i].start == MAP_FAILED) {
        std::cerr << "[loopback] v4l2 mmap " << i
                  << " failed: " << std::strerror(errno) << "\n";
        return false;
      }
      // Queue this buffer for the driver
      v4l2_buffer qbuf{};
      v4l2_plane qplanes[1]{};
      qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      qbuf.memory = V4L2_MEMORY_MMAP;
      qbuf.index = i;
      qbuf.length = 1;
      qbuf.m.planes = qplanes;
      if (ioctl(fd_, VIDIOC_QBUF, &qbuf) < 0) {
        std::cerr << "[loopback] v4l2 initial QBUF " << i
                  << " failed: " << std::strerror(errno) << "\n";
        return false;
      }
    }

    int t = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd_, VIDIOC_STREAMON, &t) < 0) {
      std::cerr << "[loopback] v4l2 STREAMON failed: "
                << std::strerror(errno) << "\n";
      return false;
    }
    std::cout << "[loopback] v4l2 stream on, " << buffers_.size()
              << " buffers\n";
    return true;
  }

  // Returns true on success; out_data populated with NV12 bytes
  // (Y plane + UV plane). Returns false on timeout or error.
  bool dequeue(std::vector<std::uint8_t> &out_data, int timeout_ms = 1000) {
    pollfd pfd{fd_, POLLIN, 0};
    int pr = poll(&pfd, 1, timeout_ms);
    if (pr <= 0)
      return false;

    v4l2_buffer buf{};
    v4l2_plane planes[1]{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.length = 1;
    buf.m.planes = planes;
    if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
      if (errno == EAGAIN)
        return false;
      std::cerr << "[loopback] v4l2 DQBUF failed: "
                << std::strerror(errno) << "\n";
      return false;
    }

    const std::size_t bytes_used =
        std::min<std::size_t>(planes[0].bytesused, buffers_[buf.index].length);
    out_data.assign(
        static_cast<std::uint8_t *>(buffers_[buf.index].start),
        static_cast<std::uint8_t *>(buffers_[buf.index].start) + bytes_used);

    // Re-queue the buffer
    v4l2_buffer qbuf{};
    v4l2_plane qplanes[1]{};
    qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    qbuf.memory = V4L2_MEMORY_MMAP;
    qbuf.index = buf.index;
    qbuf.length = 1;
    qbuf.m.planes = qplanes;
    if (ioctl(fd_, VIDIOC_QBUF, &qbuf) < 0) {
      std::cerr << "[loopback] v4l2 re-QBUF failed: "
                << std::strerror(errno) << "\n";
    }
    return true;
  }

  int width() const { return width_; }
  int height() const { return height_; }

  void close() {
    if (fd_ >= 0) {
      int t = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      ioctl(fd_, VIDIOC_STREAMOFF, &t);
      for (auto &b : buffers_) {
        if (b.start && b.start != MAP_FAILED)
          munmap(b.start, b.length);
      }
      buffers_.clear();
      ::close(fd_);
      fd_ = -1;
    }
  }

  ~V4l2Capture() { close(); }

private:
  struct Buf {
    void *start = nullptr;
    std::size_t length = 0;
  };
  int fd_ = -1;
  int width_ = 0;
  int height_ = 0;
  std::uint32_t plane_size_ = 0;
  std::vector<Buf> buffers_;
};

// ---------------------------------------------------------------
// DrmDisplay: render incoming NV12 frames directly to the board's
// MIPI panel via DRM/KMS plane composition. No EGL/GL — RV1126B has
// no Mali GPU. Dumb buffers are CPU-mapped, we memcpy NV12 in, then
// drmModeSetPlane.
//
// Caller must ensure no other DRM master holds the card (e.g. stop
// weston: `pkill weston`). drmSetMaster will be attempted; failure
// likely means another compositor is holding the card.
// ---------------------------------------------------------------
class DrmDisplay {
public:
  bool open() {
    fd_ = ::open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
      std::cerr << "[drm] open(/dev/dri/card0) failed: "
                << std::strerror(errno) << "\n";
      return false;
    }
    if (drmSetMaster(fd_) < 0) {
      std::cerr << "[drm] drmSetMaster failed: " << std::strerror(errno)
                << " — another compositor (weston?) is holding DRM master\n";
      ::close(fd_);
      fd_ = -1;
      return false;
    }
    drmModeRes *res = drmModeGetResources(fd_);
    if (!res) {
      std::cerr << "[drm] drmModeGetResources failed\n";
      return false;
    }

    // Find first connected connector
    drmModeConnector *conn = nullptr;
    for (int i = 0; i < res->count_connectors; ++i) {
      drmModeConnector *c = drmModeGetConnector(fd_, res->connectors[i]);
      if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
        conn = c;
        break;
      }
      if (c)
        drmModeFreeConnector(c);
    }
    if (!conn) {
      std::cerr << "[drm] no connected connector\n";
      drmModeFreeResources(res);
      return false;
    }
    conn_id_ = conn->connector_id;
    mode_ = conn->modes[0];
    screen_w_ = mode_.hdisplay;
    screen_h_ = mode_.vdisplay;
    std::cout << "[drm] connector " << conn_id_ << " " << screen_w_ << "x"
              << screen_h_ << "@" << mode_.vrefresh << "Hz\n";

    // Pick CRTC via current encoder
    drmModeEncoder *enc = drmModeGetEncoder(fd_, conn->encoder_id);
    if (enc) {
      crtc_id_ = enc->crtc_id;
      drmModeFreeEncoder(enc);
    } else {
      // Fallback: pick first possible CRTC
      for (int i = 0; i < res->count_encoders; ++i) {
        drmModeEncoder *e = drmModeGetEncoder(fd_, res->encoders[i]);
        if (!e) continue;
        if (e->possible_crtcs && res->count_crtcs > 0) {
          crtc_id_ = res->crtcs[0];
          drmModeFreeEncoder(e);
          break;
        }
        drmModeFreeEncoder(e);
      }
    }
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

    if (!crtc_id_) {
      std::cerr << "[drm] no CRTC\n";
      return false;
    }

    // Find an NV12-capable plane attached to our CRTC
    drmSetClientCap(fd_, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmModePlaneRes *pres = drmModeGetPlaneResources(fd_);
    if (!pres) {
      std::cerr << "[drm] drmModeGetPlaneResources failed\n";
      return false;
    }
    for (std::uint32_t i = 0; i < pres->count_planes; ++i) {
      drmModePlane *p = drmModeGetPlane(fd_, pres->planes[i]);
      if (!p) continue;
      bool can_use = false;
      // Plane must be usable on our CRTC and support NV12
      const int crtc_idx = crtcIndex();
      if (p->possible_crtcs & (1u << crtc_idx)) {
        for (std::uint32_t f = 0; f < p->count_formats; ++f) {
          if (p->formats[f] == DRM_FORMAT_NV12) {
            can_use = true;
            break;
          }
        }
      }
      if (can_use) {
        plane_id_ = p->plane_id;
        std::cout << "[drm] using plane " << plane_id_ << " (NV12)\n";
        drmModeFreePlane(p);
        break;
      }
      drmModeFreePlane(p);
    }
    drmModeFreePlaneResources(pres);
    if (!plane_id_) {
      std::cerr << "[drm] no NV12-capable plane found\n";
      return false;
    }

    return true;
  }

  // Allocate a pair of dumb buffers sized for the source video frame.
  // Source frame size is dynamic (we use the first frame's dimensions).
  bool ensureBuffers(int w, int h) {
    if (buf_w_ == w && buf_h_ == h)
      return true;
    freeBuffers();
    buf_w_ = w;
    buf_h_ = h;
    for (int i = 0; i < 2; ++i) {
      DumbBuf &b = bufs_[i];
      drm_mode_create_dumb cd{};
      cd.width = w;
      cd.height = h * 3 / 2; // NV12 = 1.5 * w * h
      cd.bpp = 8;
      if (drmIoctl(fd_, DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0) {
        std::cerr << "[drm] CREATE_DUMB failed: " << std::strerror(errno)
                  << "\n";
        return false;
      }
      b.handle = cd.handle;
      b.size = cd.size;
      b.pitch = cd.pitch;

      drm_mode_map_dumb md{};
      md.handle = b.handle;
      if (drmIoctl(fd_, DRM_IOCTL_MODE_MAP_DUMB, &md) < 0) {
        std::cerr << "[drm] MAP_DUMB failed: " << std::strerror(errno) << "\n";
        return false;
      }
      b.mapped = static_cast<std::uint8_t *>(
          mmap(nullptr, b.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_,
               md.offset));
      if (b.mapped == MAP_FAILED) {
        std::cerr << "[drm] mmap dumb failed: " << std::strerror(errno)
                  << "\n";
        return false;
      }
      std::memset(b.mapped, 0, b.size);

      // Build FB with NV12: plane 0 = Y at offset 0 with pitch.
      // plane 1 = UV right after Y, pitch same as Y (interleaved 8-bit pairs).
      std::uint32_t handles[4] = {b.handle, b.handle, 0, 0};
      std::uint32_t pitches[4] = {b.pitch, b.pitch, 0, 0};
      std::uint32_t offsets[4] = {0, b.pitch * static_cast<std::uint32_t>(h),
                                  0, 0};
      if (drmModeAddFB2(fd_, w, h, DRM_FORMAT_NV12, handles, pitches,
                        offsets, &b.fb_id, 0) < 0) {
        std::cerr << "[drm] AddFB2 failed: " << std::strerror(errno) << "\n";
        return false;
      }
    }
    std::cout << "[drm] dumb buffers ready: 2x " << w << "x" << h
              << " NV12\n";
    return true;
  }

  void renderNV12(const std::uint8_t *src, int src_w, int src_h) {
    if (!ensureBuffers(src_w, src_h))
      return;
    DumbBuf &b = bufs_[next_];
    next_ ^= 1;

    // Copy Y plane (src_w x src_h)
    if (b.pitch == static_cast<std::uint32_t>(src_w)) {
      std::memcpy(b.mapped, src, static_cast<std::size_t>(src_w) * src_h);
    } else {
      for (int y = 0; y < src_h; ++y) {
        std::memcpy(b.mapped + y * b.pitch, src + y * src_w, src_w);
      }
    }
    // Copy UV plane (src_w x src_h/2 → right after Y in the dumb buffer)
    const std::size_t uv_offset_dst = b.pitch * static_cast<std::size_t>(src_h);
    const std::uint8_t *uv_src = src + static_cast<std::size_t>(src_w) * src_h;
    if (b.pitch == static_cast<std::uint32_t>(src_w)) {
      std::memcpy(b.mapped + uv_offset_dst, uv_src,
                  static_cast<std::size_t>(src_w) * src_h / 2);
    } else {
      for (int y = 0; y < src_h / 2; ++y) {
        std::memcpy(b.mapped + uv_offset_dst + y * b.pitch,
                    uv_src + y * src_w, src_w);
      }
    }

    // Setplane: scale src_w×src_h to fullscreen
    if (drmModeSetPlane(fd_, plane_id_, crtc_id_, b.fb_id, 0,
                        /*crtc_x=*/0, /*crtc_y=*/0,
                        /*crtc_w=*/screen_w_, /*crtc_h=*/screen_h_,
                        /*src_x=*/0, /*src_y=*/0,
                        /*src_w=*/src_w << 16, /*src_h=*/src_h << 16) < 0) {
      std::cerr << "[drm] SetPlane failed: " << std::strerror(errno) << "\n";
    }
  }

  ~DrmDisplay() { close(); }

  void close() {
    freeBuffers();
    if (fd_ >= 0) {
      drmDropMaster(fd_);
      ::close(fd_);
      fd_ = -1;
    }
  }

private:
  int crtcIndex() {
    drmModeRes *res = drmModeGetResources(fd_);
    int idx = 0;
    for (int i = 0; i < res->count_crtcs; ++i) {
      if (res->crtcs[i] == crtc_id_) {
        idx = i;
        break;
      }
    }
    drmModeFreeResources(res);
    return idx;
  }

  void freeBuffers() {
    for (auto &b : bufs_) {
      if (b.fb_id) {
        drmModeRmFB(fd_, b.fb_id);
        b.fb_id = 0;
      }
      if (b.mapped && b.mapped != MAP_FAILED) {
        munmap(b.mapped, b.size);
        b.mapped = nullptr;
      }
      if (b.handle) {
        drm_mode_destroy_dumb dd{};
        dd.handle = b.handle;
        drmIoctl(fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        b.handle = 0;
      }
    }
    buf_w_ = buf_h_ = 0;
  }

  struct DumbBuf {
    std::uint32_t handle = 0;
    std::uint32_t fb_id = 0;
    std::uint32_t pitch = 0;
    std::uint64_t size = 0;
    std::uint8_t *mapped = nullptr;
  };

  int fd_ = -1;
  std::uint32_t conn_id_ = 0;
  std::uint32_t crtc_id_ = 0;
  std::uint32_t plane_id_ = 0;
  drmModeModeInfo mode_{};
  int screen_w_ = 0;
  int screen_h_ = 0;
  DumbBuf bufs_[2];
  int next_ = 0;
  int buf_w_ = 0;
  int buf_h_ = 0;
};

static DrmDisplay g_drm;
static std::atomic<bool> g_drm_ok{false};

class LoopbackDelegate : public RoomDelegate {
public:
  void onParticipantConnected(Room &,
                              const ParticipantConnectedEvent &ev) override {
    if (ev.participant) {
      std::cout << "[loopback] participant connected: "
                << ev.participant->identity() << "\n";
    }
  }

  void onParticipantDisconnected(
      Room &, const ParticipantDisconnectedEvent &ev) override {
    if (ev.participant) {
      std::cout << "[loopback] participant disconnected: "
                << ev.participant->identity() << "\n";
    }
  }

  void onTrackSubscribed(Room &room,
                         const TrackSubscribedEvent &ev) override {
    if (!ev.participant || !ev.track)
      return;
    const std::string identity = ev.participant->identity();
    const std::string track_name = ev.track->name();
    const TrackKind kind = ev.track->kind();

    const char *kind_str = (kind == TrackKind::KIND_VIDEO)   ? "video"
                           : (kind == TrackKind::KIND_AUDIO) ? "audio"
                                                              : "unknown";
    std::cout << "[loopback] track subscribed: " << identity << ":"
              << track_name << " kind=" << kind_str
              << " sid=" << ev.track->sid() << "\n";

    auto stats = g_stats.get(identity, track_name);
    if (kind == TrackKind::KIND_VIDEO) {
      // 让 SDK 产出 I420（最稳定的 YUV 格式，FFI convert pipeline 不支持
      // NV12 作为目标）。我们在 callback 里 I420→NV12（trivial 软转换）
      // 然后送 DRM。
      VideoStream::Options vopts;
      vopts.format = VideoBufferType::I420;
      vopts.capacity = 8; // ring buffer 防止积压
      room.setOnVideoFrameCallback(
          identity, track_name,
          [stats](const VideoFrame &frame, std::int64_t /*ts*/) {
            stats->video_frames.fetch_add(1, std::memory_order_relaxed);
            if (!g_drm_ok.load(std::memory_order_relaxed) ||
                frame.type() != VideoBufferType::I420)
              return;
            // I420 → NV12 软转换：Y 平面直拷，UV 按 U-V 配对插值
            const int w = frame.width();
            const int h = frame.height();
            const std::size_t y_size = static_cast<std::size_t>(w) * h;
            const std::size_t uv_each =
                static_cast<std::size_t>(w / 2) * (h / 2);
            const std::uint8_t *src = frame.data();
            const std::uint8_t *u = src + y_size;
            const std::uint8_t *v = u + uv_each;
            // 用 thread-local 缓冲避免每帧 alloc
            thread_local std::vector<std::uint8_t> nv12_buf;
            nv12_buf.resize(y_size + uv_each * 2);
            std::memcpy(nv12_buf.data(), src, y_size);
            std::uint8_t *uv = nv12_buf.data() + y_size;
            for (std::size_t i = 0; i < uv_each; ++i) {
              uv[2 * i + 0] = u[i];
              uv[2 * i + 1] = v[i];
            }
            g_drm.renderNV12(nv12_buf.data(), w, h);
          },
          vopts);
    } else if (kind == TrackKind::KIND_AUDIO) {
      // 远端音频 track 的 name 经常是空字符串（比如来自手机端 WebRTC 的
      // 默认麦），by-name 注册不命中。改用 TrackSource enum：远端麦统一
      // 报告为 SOURCE_MICROPHONE。
      const TrackSource src = ev.track->source().value_or(
          TrackSource::SOURCE_MICROPHONE);
      room.setOnAudioFrameCallback(
          identity, src, [stats](const AudioFrame &frame) {
            const auto n = stats->audio_frames.fetch_add(
                1, std::memory_order_relaxed);
            // 第一帧打印一次实际 PCM 参数，帮助排查音质问题
            if (n == 0) {
              std::cout << "[loopback] first audio frame: rate="
                        << frame.sample_rate() << "Hz channels="
                        << frame.num_channels() << " samples_per_ch="
                        << frame.samples_per_channel() << " buf_size="
                        << frame.data().size() << "\n";
            }
            // 把远端音频写到板上 ALSA codec (ES8389)，让 board 真的
            // "开口"。lazy-init 用第一帧的 sample_rate/channels。
            if (g_alsa.ensureOpen(frame.sample_rate(), frame.num_channels())) {
              const auto &samples = frame.data();
              if (!samples.empty()) {
                g_alsa.enqueue(samples.data(), frame.samples_per_channel());
              }
            }
          });
    }
  }
};

int main(int argc, char *argv[]) {
  std::string url = getenvOrEmpty("LIVEKIT_URL");
  std::string token = getenvOrEmpty("LIVEKIT_TOKEN");
  if (argc >= 3) {
    url = argv[1];
    token = argv[2];
  }
  if (url.empty() || token.empty()) {
    std::cerr << "Usage: BoardLoopback <ws-url> <token>\n"
              << "   or  LIVEKIT_URL=... LIVEKIT_TOKEN=... BoardLoopback\n";
    return 1;
  }

  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif

  livekit::initialize(LogLevel::Info, LogSink::kConsole);

  // Try to take over the DRM device for direct video plane rendering.
  // Skip with BOARD_LOOPBACK_NO_DRM=1 if running headless or weston
  // can't be stopped.
  if (!std::getenv("BOARD_LOOPBACK_NO_DRM")) {
    if (g_drm.open()) {
      g_drm_ok.store(true);
      std::cout << "[loopback] DRM display ready — incoming video will "
                   "render to MIPI panel\n";
    } else {
      std::cerr << "[loopback] DRM init failed; continuing without on-screen "
                   "render. Stop weston (e.g. `pkill weston`) and retry, "
                   "or set BOARD_LOOPBACK_NO_DRM=1 to silence.\n";
    }
  }

  auto room = std::make_unique<Room>();
  LoopbackDelegate delegate;
  room->setDelegate(&delegate);

  RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;
  if (!room->Connect(url, token, options)) {
    std::cerr << "[loopback] failed to connect\n";
    livekit::shutdown();
    return 1;
  }

  LocalParticipant *lp = room->localParticipant();
  std::cout << "[loopback] connected as identity='" << lp->identity()
            << "' room='" << room->room_info().name << "'\n";

  // Publish video. Try opening the on-board V4L2 camera (rkisp_mainpath
  // via /dev/video-camera0); fall back to synthetic gradient if it fails
  // or BOARD_LOOPBACK_SYNTH_VIDEO=1 is set.
  const bool synth_video =
      std::getenv("BOARD_LOOPBACK_SYNTH_VIDEO") != nullptr;
  std::unique_ptr<V4l2Capture> v4l2;
  int video_w = kWidth, video_h = kHeight;
  if (!synth_video) {
    const char *v4l2_dev_env = std::getenv("V4L2_DEVICE");
    const char *v4l2_dev =
        (v4l2_dev_env && *v4l2_dev_env) ? v4l2_dev_env : "/dev/video-camera0";
    v4l2 = std::make_unique<V4l2Capture>();
    if (v4l2->open(v4l2_dev, kWidth, kHeight) && v4l2->startStream()) {
      video_w = v4l2->width();
      video_h = v4l2->height();
    } else {
      std::cerr << "[loopback] v4l2 init failed; falling back to synth gradient\n";
      v4l2.reset();
    }
  }
  auto video_source = std::make_shared<VideoSource>(video_w, video_h);
  std::shared_ptr<LocalVideoTrack> video_track = lp->publishVideoTrack(
      kVideoTrackName, video_source, TrackSource::SOURCE_CAMERA);
  std::cout << "[loopback] publishing video '" << kVideoTrackName << "' "
            << video_w << "x" << video_h << " ("
            << (v4l2 ? "live camera NV12" : "synth gradient RGBA") << ")\n";

  // Publish audio from the board's real microphone (ES8389 codec capture
  // via ALSA). Falls back to silence if capture device can't open. Set
  // BOARD_LOOPBACK_SYNTH_AUDIO=1 to override and use a 440Hz sine instead
  // (useful for environment that has no mic or for repro the older
  // synthetic-only mode).
  const bool synth_audio =
      std::getenv("BOARD_LOOPBACK_SYNTH_AUDIO") != nullptr;
  auto audio_source = std::make_shared<AudioSource>(kAudioSampleRate,
                                                    kAudioChannels);
  std::shared_ptr<LocalAudioTrack> audio_track = lp->publishAudioTrack(
      kAudioTrackName, audio_source, TrackSource::SOURCE_MICROPHONE);

  // Try opening ALSA capture (mic). If that succeeds, the audio_thread
  // below pumps real mic samples; otherwise it falls back to synthetic
  // (or refuses if synth_audio mode is off and capture failed).
  snd_pcm_t *capture_pcm = nullptr;
  if (!synth_audio) {
    const char *cap_dev_env = std::getenv("ALSA_CAPTURE_DEVICE");
    const char *cap_dev =
        (cap_dev_env && *cap_dev_env) ? cap_dev_env : "default";
    int err = snd_pcm_open(&capture_pcm, cap_dev, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
      std::cerr << "[loopback] alsa capture open(" << cap_dev
                << ") failed: " << snd_strerror(err)
                << " — falling back to synthetic 440Hz tone\n";
      capture_pcm = nullptr;
    } else {
      err = snd_pcm_set_params(capture_pcm, SND_PCM_FORMAT_S16_LE,
                               SND_PCM_ACCESS_RW_INTERLEAVED,
                               kAudioChannels, kAudioSampleRate,
                               /*soft_resample=*/1,
                               /*latency_us=*/200000); // 200ms 录音 buffer
      if (err < 0) {
        std::cerr << "[loopback] alsa capture set_params failed: "
                  << snd_strerror(err) << " — falling back to sine\n";
        snd_pcm_close(capture_pcm);
        capture_pcm = nullptr;
      } else {
        std::cout << "[loopback] alsa capture opened: " << cap_dev
                  << " " << kAudioSampleRate << "Hz " << kAudioChannels
                  << "ch s16le 200ms-buf\n";
      }
    }
  }
  std::cout << "[loopback] publishing audio '" << kAudioTrackName << "' "
            << kAudioSampleRate << "Hz mono "
            << (capture_pcm ? "live mic" : "synth 440Hz sine") << "\n";

  // ES8389 mic 默认增益偏低，先用纯软件 gain 提一下。BOARD_LOOPBACK_MIC_GAIN
  // 可改（推荐 4-12，>16 容易削顶）。1.0 关闭软件放大走原始电平。
  // 默认 12× —— 在 ATK-DLRV1126B 板载 mic 上实测人声清晰可辨。
  float mic_gain = 12.0f;
  if (const char *g_env = std::getenv("BOARD_LOOPBACK_MIC_GAIN")) {
    try {
      mic_gain = std::stof(g_env);
    } catch (...) {
      // ignore parse error, keep default
    }
  }
  if (capture_pcm) {
    std::cout << "[loopback] mic software gain = " << mic_gain << "×\n";
  }

  std::thread audio_thread([&, capture_pcm]() {
    std::vector<std::int16_t> buf(kAudioSamplesPerChannel * kAudioChannels);
    double phase = 0.0;
    const double phase_step = 2.0 * 3.14159265358979323846 *
                              kAudioToneHz / kAudioSampleRate;
    while (g_running.load()) {
      if (capture_pcm) {
        // Real mic path: blocking read of 10ms (480) frames
        snd_pcm_sframes_t got = snd_pcm_readi(
            capture_pcm, buf.data(), kAudioSamplesPerChannel);
        if (got < 0) {
          const int e = static_cast<int>(got);
          got = snd_pcm_recover(capture_pcm, e, /*silent=*/1);
          if (got < 0) {
            std::cerr << "[loopback] alsa capture err: "
                      << snd_strerror(static_cast<int>(got)) << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
          }
          // recovered — treat as a single dropped frame, continue
          continue;
        }
        if (got < kAudioSamplesPerChannel) {
          // Short read — pad rest with zeros
          for (size_t i = got * kAudioChannels; i < buf.size(); ++i)
            buf[i] = 0;
        }
        // Apply software gain with hard clipping at int16 range
        if (mic_gain != 1.0f) {
          for (auto &s : buf) {
            const float v = static_cast<float>(s) * mic_gain;
            s = static_cast<std::int16_t>(
                std::max(-32768.0f, std::min(32767.0f, v)));
          }
        }
      } else {
        // Fallback synthetic 440Hz
        for (int i = 0; i < kAudioSamplesPerChannel; ++i) {
          buf[i] = static_cast<std::int16_t>(
              kAudioAmplitude * std::sin(phase));
          phase += phase_step;
          if (phase > 2.0 * 3.14159265358979323846)
            phase -= 2.0 * 3.14159265358979323846;
        }
      }

      AudioFrame frame(buf, kAudioSampleRate, kAudioChannels,
                       kAudioSamplesPerChannel);
      try {
        audio_source->captureFrame(frame, /*timeout_ms=*/100);
      } catch (const std::exception &e) {
        std::cerr << "[loopback] audio capture push err: " << e.what()
                  << "\n";
      }

      if (!capture_pcm) {
        // Synth mode — pace ourselves; mic mode is naturally paced by
        // snd_pcm_readi blocking on hardware.
        std::this_thread::sleep_for(
            std::chrono::milliseconds(kAudioFrameMs));
      }
    }

    if (capture_pcm) {
      snd_pcm_drop(capture_pcm);
      snd_pcm_close(capture_pcm);
    }
  });

  const auto t0 = std::chrono::steady_clock::now();
  auto last_report = t0;
  std::uint64_t frame_count = 0;

  std::cout << "[loopback] running until Ctrl-C; reporting every "
            << kReportEverySeconds << "s.\n";

  while (g_running.load()) {
    if (v4l2) {
      // Real V4L2 capture — pull next NV12 frame from rkisp_mainpath
      std::vector<std::uint8_t> bytes;
      if (v4l2->dequeue(bytes, /*timeout_ms=*/200)) {
        VideoFrame vf(video_w, video_h, VideoBufferType::NV12,
                      std::move(bytes));
        video_source->captureFrame(std::move(vf));
      } else {
        // Timeout — camera may be slow to bring up; just skip this iteration
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
        continue;
      }
    } else {
      // Synthetic fallback: time-modulated RGBA gradient
      VideoFrame vf = VideoFrame::create(video_w, video_h,
                                         VideoBufferType::RGBA);
      const auto t_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
      const std::uint8_t hue = static_cast<std::uint8_t>((t_ms / 30) & 0xff);
      std::uint8_t *p = vf.data();
      if (p) {
        for (int y = 0; y < video_h; ++y) {
          for (int x = 0; x < video_w; ++x) {
            p[0] = static_cast<std::uint8_t>(x * 255 / video_w);  // R
            p[1] = static_cast<std::uint8_t>(y * 255 / video_h);  // G
            p[2] = hue;                                            // B
            p[3] = 255;                                            // A
            p += 4;
          }
        }
      }
      video_source->captureFrame(std::move(vf));
    }

    // 注：data track publish 在当前 server/NAT 路径下 SCTP 协商超时，
    // 已从该 example 移除以避免每次都报错（见 phase5-summary §polish 项）。
    // 视频/音频是主轨道走 RTP/SRTP，不受 SCTP 协商失败影响。

    // Periodic report
    const auto now = std::chrono::steady_clock::now();
    if (now - last_report >= std::chrono::seconds(kReportEverySeconds)) {
      last_report = now;
      const auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                            now - t0)
                            .count();
      std::cout << "[loopback] T+" << secs << "s  published " << frame_count
                << " video frames; alsa_underruns="
                << g_alsa.underruns()
                << " alsa_dropped=" << g_alsa.queue_dropped() << "\n";
      std::cout << "[loopback] remote streams:\n";
      g_stats.print(std::cout);
    }

    ++frame_count;
    // V4L2 dequeue blocks on poll → naturally paced by camera fps;
    // synth path needs explicit pacing.
    if (!v4l2)
      std::this_thread::sleep_for(std::chrono::milliseconds(33));
  }

  std::cout << "[loopback] final stats:\n";
  g_stats.print(std::cout);

  std::cout << "[loopback] disconnecting...\n";
  if (audio_thread.joinable())
    audio_thread.join();
  // Drop tracks first, then room
  audio_track.reset();
  audio_source.reset();
  video_track.reset();
  room.reset();
  livekit::shutdown();

  std::cout << "[loopback] done.\n";
  return 0;
}
