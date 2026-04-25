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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
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
      room.setOnVideoFrameCallback(
          identity, track_name,
          [stats](const VideoFrame & /*frame*/, std::int64_t /*ts*/) {
            stats->video_frames.fetch_add(1, std::memory_order_relaxed);
          });
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

  // Publish synthetic video
  auto video_source = std::make_shared<VideoSource>(kWidth, kHeight);
  std::shared_ptr<LocalVideoTrack> video_track = lp->publishVideoTrack(
      kVideoTrackName, video_source, TrackSource::SOURCE_CAMERA);
  std::cout << "[loopback] publishing video '" << kVideoTrackName << "' "
            << kWidth << "x" << kHeight << "\n";

  // Publish synthetic 440Hz sine wave audio so peer can hear the board.
  // 48kHz mono 20ms PCM16 frames pumped from a dedicated thread.
  auto audio_source = std::make_shared<AudioSource>(kAudioSampleRate,
                                                    kAudioChannels);
  std::shared_ptr<LocalAudioTrack> audio_track = lp->publishAudioTrack(
      kAudioTrackName, audio_source, TrackSource::SOURCE_MICROPHONE);
  std::cout << "[loopback] publishing audio '" << kAudioTrackName << "' "
            << kAudioSampleRate << "Hz mono sine " << kAudioToneHz << "Hz\n";

  std::thread audio_thread([&]() {
    double phase = 0.0;
    const double phase_step = 2.0 * 3.14159265358979323846 *
                              kAudioToneHz / kAudioSampleRate;
    while (g_running.load()) {
      AudioFrame frame = AudioFrame::create(
          kAudioSampleRate, kAudioChannels, kAudioSamplesPerChannel);
      auto *samples = const_cast<std::int16_t *>(frame.data().data());
      for (int i = 0; i < kAudioSamplesPerChannel; ++i) {
        samples[i] = static_cast<std::int16_t>(
            kAudioAmplitude * std::sin(phase));
        phase += phase_step;
        if (phase > 2.0 * 3.14159265358979323846) {
          phase -= 2.0 * 3.14159265358979323846;
        }
      }
      try {
        audio_source->captureFrame(frame, /*timeout_ms=*/100);
      } catch (const std::exception &e) {
        std::cerr << "[loopback] audio capture err: " << e.what() << "\n";
      }
      // Pacing matches frame duration (20ms) — captureFrame in real-time
      // mode returns immediately so we sleep here.
      std::this_thread::sleep_for(std::chrono::milliseconds(kAudioFrameMs));
    }
  });

  const auto t0 = std::chrono::steady_clock::now();
  auto last_report = t0;
  std::uint64_t frame_count = 0;

  std::cout << "[loopback] running until Ctrl-C; reporting every "
            << kReportEverySeconds << "s.\n";

  while (g_running.load()) {
    // Generate a time-modulated RGBA gradient
    VideoFrame vf = VideoFrame::create(kWidth, kHeight, VideoBufferType::RGBA);
    const auto t_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
    const std::uint8_t hue = static_cast<std::uint8_t>((t_ms / 30) & 0xff);
    std::uint8_t *p = vf.data();
    if (p) {
      for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
          p[0] = static_cast<std::uint8_t>(x * 255 / kWidth);  // R: x
          p[1] = static_cast<std::uint8_t>(y * 255 / kHeight); // G: y
          p[2] = hue;                                          // B: time
          p[3] = 255;                                          // A
          p += 4;
        }
      }
    }
    video_source->captureFrame(std::move(vf));

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
    // ~30 fps
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
