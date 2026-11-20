#ifndef BEND_AUDIO_C
#define BEND_AUDIO_C
// Audio
// =====


// The ring: 4096 float32 stereo frames. The effects fill it on the
// evaluator's thread; the device's callback drains it and pads the
// rest of its buffer with silence. All audio effects share this source;
// each entry is present only when its effect is reachable.
#ifndef IO_RING
#define IO_RING 4096u

#ifdef __OBJC__
#import <AudioToolbox/AudioToolbox.h>
#elif defined(__linux__)
#include <alsa/asoundlib.h>
#elif defined(_WIN32)
#pragma push_macro("FAR")
#pragma push_macro("NEAR")
#undef FAR
#undef NEAR
#define FAR
#define NEAR
#include <mmsystem.h>
#include <mmreg.h>
#pragma pop_macro("NEAR")
#pragma pop_macro("FAR")
#endif

typedef struct {
  _Atomic(u64) read, written;
  float        pcm[IO_RING * 2];
#ifdef __OBJC__
  AudioUnit    unit;
#elif defined(__linux__)
  snd_pcm_t*   unit;
  pthread_t    pump;
  _Atomic(u32) done;
#elif defined(_WIN32)
  HWAVEOUT     unit;
  pthread_t    pump;
  _Atomic(u32) done;
  // The chunk and its header outlive the pump thread: the device reads
  // the chunk after the write returns, so neither may live on its stack.
  float        out[256 * 2];
  WAVEHDR      hdr;
  HWAVEIN      in;
  pthread_t    in_pump;
  _Atomic(u32) in_done;
  _Atomic(u64) cap_read, cap_written;
  float        cap[IO_RING * 2];
  short        in_pcm[256 * 2];
  WAVEHDR      in_hdr;
  u32          in_dev;
#endif
} IoRing;

// n frames queued after the write; a write past the ring's room is
// dropped and the queue answered as it is.
static u64 io_ring_write(IoRing* p, const float* pcm, u32 n) {
  u64 w = atomic_load_explicit(&p->written, memory_order_relaxed);
  u64 r = atomic_load_explicit(&p->read, memory_order_acquire);
  if (w - r + n <= IO_RING) {
    u32 at    = (u32)(w % IO_RING);
    u32 first = n < IO_RING - at ? n : IO_RING - at;
    memcpy(p->pcm + at * 2, pcm, first * 8);
    memcpy(p->pcm, pcm + first * 2, (n - first) * 8);
    atomic_store_explicit(&p->written, w + n, memory_order_release);
    w += n;
  }
  return w - r;
}

static void io_ring_pull(IoRing* p, float* out, u32 frames) {
  u64 r     = atomic_load_explicit(&p->read, memory_order_relaxed);
  u64 w     = atomic_load_explicit(&p->written, memory_order_acquire);
  u32 n     = (u32)(w - r < frames ? w - r : frames);
  u32 at    = (u32)(r % IO_RING);
  u32 first = n < IO_RING - at ? n : IO_RING - at;
  memcpy(out, p->pcm + at * 2, first * 8);
  memcpy(out + first * 2, p->pcm, (n - first) * 8);
  memset(out + n * 2, 0, (frames - n) * 8);
  atomic_store_explicit(&p->read, r + n, memory_order_release);
}

#ifdef __OBJC__

static OSStatus io_ring_pump(void* ctx, AudioUnitRenderActionFlags* flags,
  const AudioTimeStamp* when, UInt32 bus, UInt32 frames,
  AudioBufferList* bl) {
  if (bl->mNumberBuffers != 1 || bl->mBuffers[0].mNumberChannels != 2
      || bl->mBuffers[0].mDataByteSize < frames * 8) {
    return kAudio_ParamError;
  }
  io_ring_pull(ctx, bl->mBuffers[0].mData, frames);
  return noErr;
}

// The default output unit fed float32 stereo at the asked rate (the
// unit converts to the device's own).
static u32 io_ring_start(IoRing* p, u32 rate) {
  AudioComponentDescription desc = { kAudioUnitType_Output,
    kAudioUnitSubType_DefaultOutput, kAudioUnitManufacturer_Apple, 0, 0 };
  AudioComponent comp = AudioComponentFindNext(NULL, &desc);
  if (comp == NULL || AudioComponentInstanceNew(comp, &p->unit) != noErr) {
    return ENODEV;
  }
  AudioStreamBasicDescription fmt = { 0 };
  fmt.mSampleRate       = rate;
  fmt.mFormatID         = kAudioFormatLinearPCM;
  fmt.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  fmt.mBytesPerPacket   = 8;
  fmt.mFramesPerPacket  = 1;
  fmt.mBytesPerFrame    = 8;
  fmt.mChannelsPerFrame = 2;
  fmt.mBitsPerChannel   = 32;
  AURenderCallbackStruct cb = { io_ring_pump, p };
  bool ok = AudioUnitSetProperty(p->unit, kAudioUnitProperty_StreamFormat,
      kAudioUnitScope_Input, 0, &fmt, sizeof fmt) == noErr
    && AudioUnitSetProperty(p->unit, kAudioUnitProperty_SetRenderCallback,
      kAudioUnitScope_Input, 0, &cb, sizeof cb) == noErr
    && AudioUnitInitialize(p->unit) == noErr
    && AudioOutputUnitStart(p->unit) == noErr;
  return ok ? 0 : ENODEV;
}

static void io_ring_free(IoRing* p) {
  if (p->unit != NULL) {
    AudioOutputUnitStop(p->unit);
    AudioUnitUninitialize(p->unit);
    AudioComponentInstanceDispose(p->unit);
  }
  free(p);
}

#elif defined(__linux__)

// A thread feeds the default ALSA device 256 frames at a time (a
// write blocks until the device has room, so the ring drains at the
// device's clock). A chunk stays until the device has taken all of it;
// an error ALSA can't recover stops the thread, and the ring fills as
// with a stalled device.
static void* io_ring_pump(void* ctx) {
  IoRing* p  = ctx;
  float   out[256 * 2];
  u32     at = 256;
  while (atomic_load_explicit(&p->done, memory_order_relaxed) == 0) {
    if (at == 256) {
      io_ring_pull(p, out, 256);
      at = 0;
    }
    snd_pcm_sframes_t n = snd_pcm_writei(p->unit, out + at * 2, 256 - at);
    if (n > 0) {
      at += (u32)n;
    } else if (n == 0 || snd_pcm_recover(p->unit, (int)n, 1) < 0) {
      break;
    }
  }
  return NULL;
}

static void io_ring_hush(const char* file, int line, const char* fn, int err,
  const char* fmt, ...) {
}

static u32 io_ring_start(IoRing* p, u32 rate) {
  snd_lib_error_set_handler(io_ring_hush);
  if (snd_pcm_open(&p->unit, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
    return ENODEV;
  }
  if (snd_pcm_set_params(p->unit, SND_PCM_FORMAT_FLOAT_LE,
    SND_PCM_ACCESS_RW_INTERLEAVED, 2, rate, 1, 20000) < 0
    || pthread_create(&p->pump, NULL, io_ring_pump, p) != 0) {
    return ENODEV;
  }
  return 0;
}

static void io_ring_free(IoRing* p) {
  if (p->unit != NULL) {
    atomic_store_explicit(&p->done, 1, memory_order_relaxed);
    if (p->pump != 0) {
      pthread_join(p->pump, NULL);
    }
    snd_pcm_close(p->unit);
  }
  free(p);
}

#elif defined(_WIN32)

// A thread feeds waveOut 256 float32 stereo frames at a time. The device
// blocks in waveOutWrite, so the ring drains at the device's clock. The
// header and its chunk live in the ring, not on the thread's stack: a
// write returns while the device still reads the chunk.
static void* io_ring_pump(void* ctx) {
  IoRing* p = ctx;
  p->hdr.lpData = (LPSTR)p->out;
  p->hdr.dwBufferLength = sizeof p->out;
  while (atomic_load_explicit(&p->done, memory_order_relaxed) == 0) {
    io_ring_pull(p, p->out, 256);
    p->hdr.dwFlags = 0;
    if (waveOutPrepareHeader(p->unit, &p->hdr, sizeof p->hdr)
        != MMSYSERR_NOERROR) {
      Sleep(5);
      continue;
    }
    if (waveOutWrite(p->unit, &p->hdr, sizeof p->hdr) != MMSYSERR_NOERROR) {
      waveOutUnprepareHeader(p->unit, &p->hdr, sizeof p->hdr);
      Sleep(5);
      continue;
    }
    while (!(p->hdr.dwFlags & WHDR_DONE)) {
      // Asked to stop: the device still reads the chunk until the reset,
      // which marks every pending buffer done and returns it.
      if (atomic_load_explicit(&p->done, memory_order_relaxed) != 0) {
        waveOutReset(p->unit);
        break;
      }
      Sleep(1);
    }
    waveOutUnprepareHeader(p->unit, &p->hdr, sizeof p->hdr);
  }
  return NULL;
}

// Capture is one waveIn buffer recycled by a pump thread. The device fills
// 16-bit stereo; the pump converts it to float into a second ring, so a
// read never waits on the device. A machine with no input device still
// opens: reads answer silence, which keeps the tests and headless runs
// deterministic.
static void io_cap_pull(IoRing* p, float* out, u32 frames) {
  u64 r = atomic_load_explicit(&p->cap_read, memory_order_relaxed);
  u64 w = atomic_load_explicit(&p->cap_written, memory_order_acquire);
  u32 n = (u32)(w - r < frames ? w - r : frames);
  u32 at = (u32)(r % IO_RING);
  u32 first = n < IO_RING - at ? n : IO_RING - at;
  memcpy(out, p->cap + at * 2, first * 8);
  memcpy(out + first * 2, p->cap, (n - first) * 8);
  memset(out + n * 2, 0, (frames - n) * 8);
  atomic_store_explicit(&p->cap_read, r + n, memory_order_release);
}

static void* io_cap_pump(void* ctx) {
  IoRing* p = ctx;
  WAVEHDR* h = &p->in_hdr;
  h->lpData = (LPSTR)p->in_pcm;
  h->dwBufferLength = sizeof p->in_pcm;
  if (waveInPrepareHeader(p->in, h, sizeof *h) != MMSYSERR_NOERROR) {
    return NULL;
  }
  if (waveInAddBuffer(p->in, h, sizeof *h) != MMSYSERR_NOERROR) {
    waveInUnprepareHeader(p->in, h, sizeof *h);
    return NULL;
  }
  waveInStart(p->in);
  while (atomic_load_explicit(&p->in_done, memory_order_relaxed) == 0) {
    if (!(h->dwFlags & WHDR_DONE)) {
      Sleep(1);
      continue;
    }
    float pcm[256 * 2];
    u32 n = h->dwBytesRecorded / 4;
    for (u32 i = 0; i < n && i < 256 * 2; i += 1) {
      pcm[i] = (float)p->in_pcm[i] / 32768.f;
    }
    u64 w = atomic_load_explicit(&p->cap_written, memory_order_relaxed);
    u64 r = atomic_load_explicit(&p->cap_read, memory_order_acquire);
    if (w - r + n / 2 <= IO_RING) {
      u32 at = (u32)(w % IO_RING);
      u32 first = n / 2 < IO_RING - at ? n / 2 : IO_RING - at;
      memcpy(p->cap + at * 2, pcm, first * 8);
      memcpy(p->cap, pcm + first * 2, (n / 2 - first) * 8);
      atomic_store_explicit(&p->cap_written, w + n / 2, memory_order_release);
    }
    h->dwFlags = 0;
    h->dwBytesRecorded = 0;
    if (waveInAddBuffer(p->in, h, sizeof *h) != MMSYSERR_NOERROR) {
      break;
    }
  }
  waveInStop(p->in);
  waveInReset(p->in);
  waveInUnprepareHeader(p->in, h, sizeof *h);
  waveInClose(p->in);
  return NULL;
}

static u32 io_capture_start(IoRing* p, const char* name, u32 rate) {
  WAVEFORMATEX fmt = { 0 };
  fmt.wFormatTag      = WAVE_FORMAT_PCM;
  fmt.nChannels       = 2;
  fmt.nSamplesPerSec  = rate;
  fmt.wBitsPerSample  = 16;
  fmt.nBlockAlign     = 4;
  fmt.nAvgBytesPerSec = rate * 4;
  UINT n = waveInGetNumDevs();
  UINT dev = WAVE_MAPPER;
  if (name != NULL && name[0] != 0 && strcmp(name, "default") != 0) {
    dev = n;
    for (UINT i = 0; i < n; i += 1) {
      WAVEINCAPSW cap;
      if (waveInGetDevCapsW(i, &cap, sizeof cap) != MMSYSERR_NOERROR) {
        continue;
      }
      char utf[sizeof cap.szPname * 3];
      int k = WideCharToMultiByte(CP_UTF8, 0, cap.szPname, -1, utf,
        sizeof utf, NULL, NULL);
      if (k > 0 && strcmp(utf, name) == 0) {
        dev = i;
        break;
      }
    }
    if (dev == n) {
      return ENODEV;
    }
  } else if (n == 0) {
    // No device: a silent capture still answers reads.
    return 0;
  }
  if (waveInOpen(&p->in, dev, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
    return ENODEV;
  }
  p->in_dev = dev;
  if (pthread_create(&p->in_pump, NULL, io_cap_pump, p) != 0) {
    waveInClose(p->in);
    p->in = NULL;
    return ENODEV;
  }
  return 0;
}

static void io_capture_free(IoRing* p) {
  if (p->in != NULL) {
    atomic_store_explicit(&p->in_done, 1, memory_order_relaxed);
    if (p->in_pump != 0) {
      pthread_join(p->in_pump, NULL);
    }
    p->in = NULL;
  }
}

static u32 io_ring_start(IoRing* p, u32 rate) {
  WAVEFORMATEX fmt = { 0 };
  fmt.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
  fmt.nChannels       = 2;
  fmt.nSamplesPerSec  = rate;
  fmt.wBitsPerSample  = 32;
  fmt.nBlockAlign     = 8;
  fmt.nAvgBytesPerSec = rate * 8;
  if (waveOutOpen(&p->unit, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL)
      != MMSYSERR_NOERROR) {
    return ENODEV;
  }
  if (pthread_create(&p->pump, NULL, io_ring_pump, p) != 0) {
    waveOutClose(p->unit);
    p->unit = NULL;
    return ENODEV;
  }
  return 0;
}

static void io_ring_free(IoRing* p) {
  io_capture_free(p);
  if (p->unit != NULL) {
    atomic_store_explicit(&p->done, 1, memory_order_relaxed);
    waveOutReset(p->unit);
    if (p->pump != 0) {
      pthread_join(p->pump, NULL);
    }
    waveOutClose(p->unit);
  }
  free(p);
}

#else

static u32 io_ring_start(IoRing* p, u32 rate) {
  return ENOTSUP;
}

static void io_ring_free(IoRing* p) {
  free(p);
}

#endif
#endif

#ifdef CID(Audio.open)
Term audio_open_run(Env e, Term* f, IoWork* w) {
  u32     rate = (u32)f[0];
  IoRing* p    = io_mem(calloc(1, sizeof *p));
  u32     code = rate < 8000 || rate > 192000 ? EINVAL : io_ring_start(p, rate);
  if (code != 0) {
    io_ring_free(p);
    return io_fail(e, code, code == EINVAL ? NULL : "Audio.open: no audio output");
  }
  return io_done(e, io_hand((intptr_t)p));
}

static void __attribute__((constructor)) audio_open_use(void) {
  io_eff(CID(Audio.open), audio_open_run);
}
#endif

#ifdef CID(Audio.write)
// The samples (interleaved L R ...) into the ring; the frames queued
// after the write. Past the ring's room, the samples are dropped and
// the queue answered as it is.
Term audio_write_run(Env e, Term* f, IoWork* w) {
  IoRing* p   = (IoRing*)(uintptr_t)io_hand_v(f[0]);
  float   pcm[IO_RING * 2];
  u32     n   = 0;
  Term    s   = f[1];
  while (term_aux(s) == CID(Con)) {
    Term fb[2];
    spare_free(e, cls_fit(2), ctr_take(e, s, 2, fb));
    if (n < IO_RING * 2) {
      pcm[n] = f32_unbox(fb[0]);
    }
    n += 1;
    s  = fb[1];
  }
  u64 q = io_ring_write(p, pcm, n > IO_RING * 2 ? 0 : n / 2);
  return io_tup(e, f[0], q);
}

static void __attribute__((constructor)) audio_write_use(void) {
  io_eff(CID(Audio.write), audio_write_run);
}
#endif

#ifdef CID(Audio.close)
Term audio_close_run(Env e, Term* f, IoWork* w) {
  io_ring_free((IoRing*)(uintptr_t)io_hand_v(f[0]));
  return term_pak(CID(Unit), 0);
}

static void __attribute__((constructor)) audio_close_use(void) {
  io_eff(CID(Audio.close), audio_close_run);
}
#endif

#endif

#ifndef _WIN32
static u32 io_capture_start(IoRing* p, const char* name, u32 rate) {
  return ENOTSUP;
}
#endif
#ifdef CID(Audio.open_input)
Term audio_open_input_run(Env e, Term* f, IoWork* w) {
  u64 len;
  char* name = io_cstr(e, f[0], &len);
  u32 rate = (u32)f[1];
  IoRing* p = io_mem(calloc(1, sizeof *p));
  u32 code = rate < 8000 || rate > 192000 ? EINVAL
    : io_capture_start(p, name, rate);
  free(name);
  if (code != 0) {
    io_ring_free(p);
    return io_fail(e, code, NULL);
  }
  return io_done(e, io_hand((intptr_t)p));
}
static void __attribute__((constructor)) audio_open_input_use(void) {
  io_eff(CID(Audio.open_input), audio_open_input_run);
}
#endif
