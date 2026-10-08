/* Headless libretro frontend for the core's regression tests.
 *
 * usage: frontend core.so frames [content]
 *
 * Boots the core with the software renderer, flips the GPU thread and
 * CD readahead options while running, then optionally:
 *   RAMDUMP=offset:count  print count RAM words starting at offset
 *   STATEFUZZ=iters       corrupt and load save states, iters times
 *   SEED=n                seed for STATEFUZZ
 *   STATEFUZZ_SECTION=s   only corrupt the state section with marker s
 *   STATEFUZZ_LOG=1       print each corrupted byte before loading
 *   STATEFUZZ_DUMP=file   write the uncorrupted state to file
 *   STATEFUZZ_PATCH=o:v,… load the state with these bytes changed instead of fuzzing
 * Core options come from FE_CPU, FE_FASTMEM and FE_GPUTHREAD; the BIOS
 * and save directory is FE_DIR. A load that runs for more than a minute
 * is reported as a hang (exit code 3). */
#include <dlfcn.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libretro.h"

static const char* opt_gpu_thread = "true";
static const char* opt_readahead = "8";
static const char* opt_fastmem = "MMap";
static const char* opt_cpu = "Recompiler";
static int options_dirty = 0;

static void log_cb(enum retro_log_level level, const char* fmt, ...)
{
  va_list ap;
  if (level < RETRO_LOG_WARN && !getenv("VERBOSE"))
    return;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
}

static bool env_cb(unsigned cmd, void* data)
{
  switch (cmd)
  {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      ((struct retro_log_callback*)data)->log = log_cb;
      return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char**)data = getenv("FE_DIR");
      return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
      return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *(bool*)data = options_dirty != 0;
      options_dirty = 0;
      return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
      struct retro_variable* v = (struct retro_variable*)data;
      v->value = NULL;
      if (!strcmp(v->key, "swanstation_GPU_Renderer"))
        v->value = "Software";
      else if (!strcmp(v->key, "swanstation_GPU_UseThread"))
        v->value = opt_gpu_thread;
      else if (!strcmp(v->key, "swanstation_CDROM_ReadThread"))
        v->value = "true";
      else if (!strcmp(v->key, "swanstation_CDROM_ReadaheadSectors"))
        v->value = opt_readahead;
      else if (!strcmp(v->key, "swanstation_CPU_FastmemMode"))
        v->value = opt_fastmem;
      else if (!strcmp(v->key, "swanstation_CPU_ExecutionMode"))
        v->value = opt_cpu;
      return v->value != NULL;
    }
    default:
      return false;
  }
}

static void video_cb(const void* data, unsigned w, unsigned h, size_t pitch)
{
  (void)data;
  (void)w;
  (void)h;
  (void)pitch;
}
static void audio_cb(int16_t l, int16_t r)
{
  (void)l;
  (void)r;
}
static size_t audio_batch_cb(const int16_t* d, size_t f)
{
  (void)d;
  return f;
}
static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned p, unsigned d, unsigned i, unsigned id)
{
  (void)p;
  (void)d;
  (void)i;
  (void)id;
  return 0;
}

typedef void (*fn_void_t)(void);
typedef void (*fn_set_t)(void*);
typedef bool (*fn_load_game_t)(const struct retro_game_info*);
typedef size_t (*fn_serialize_size_t)(void);
typedef bool (*fn_serialize_t)(void*, size_t);
typedef bool (*fn_unserialize_t)(const void*, size_t);
typedef void* (*fn_memory_data_t)(unsigned);

static void* lookup(void* h, const char* name)
{
  void* p = dlsym(h, name);
  if (!p)
  {
    fprintf(stderr, "missing %s\n", name);
    exit(1);
  }
  return p;
}

static unsigned fuzz_iteration;
static void alarm_handler(int sig)
{
  (void)sig;
  fprintf(stderr, "HANG at state fuzz iteration %u\n", fuzz_iteration);
  _exit(3);
}

static uint32_t rng_state;
static uint32_t rng(void)
{
  rng_state = rng_state * 1103515245u + 12345u;
  return rng_state >> 8;
}

/* Corrupts bytes just after a random section marker; the bulk RAM/VRAM payloads are not interesting. */
static void corrupt_state(unsigned char* buf, const unsigned char* orig, size_t size)
{
  static const char* const markers[] = {"CPU", "DMA",    "InterruptController", "GPU",        "CDROM",
                                        "Pad", "Timers", "SPU",                 "MDEC",       "SIO",
                                        "Events", "Controller", "MemoryCard"};
  static const size_t windows[] = {1200, 200, 64, 700, 22000, 400, 120, 2600, 3200, 64, 2000, 200, 300};
  const unsigned num_markers = sizeof(markers) / sizeof(markers[0]);
  unsigned flips = 1 + rng() % 8;
  unsigned k;

  memcpy(buf, orig, size);
  for (k = 0; k < flips; k++)
  {
    const unsigned mi = rng() % num_markers;
    if (getenv("STATEFUZZ_SECTION") && strcmp(getenv("STATEFUZZ_SECTION"), markers[mi]) != 0)
      continue;
    const size_t mlen = strlen(markers[mi]);
    size_t off = 0, p;
    unsigned found = 0;
    unsigned char v;

    for (p = 0; p + mlen + 4 < size; p++)
    {
      uint32_t l;
      memcpy(&l, orig + p, 4);
      if (l == mlen && !memcmp(orig + p + 4, markers[mi], mlen))
      {
        found++;
        if (rng() % found == 0)
          off = p + 4 + mlen;
      }
    }
    if (!found)
      continue;

    off += rng() % windows[mi];
    if (off >= size)
      continue;

    v = (unsigned char)rng();
    if (rng() & 1)
      v = (rng() & 1) ? 0xFF : 0x7F;
    buf[off] = v;
  }
}

static int state_fuzz(void* h, unsigned iters, uint32_t seed)
{
  fn_void_t run = (fn_void_t)lookup(h, "retro_run");
  fn_serialize_size_t serialize_size = (fn_serialize_size_t)lookup(h, "retro_serialize_size");
  fn_serialize_t serialize = (fn_serialize_t)lookup(h, "retro_serialize");
  fn_unserialize_t unserialize = (fn_unserialize_t)lookup(h, "retro_unserialize");
  size_t size = serialize_size();
  unsigned char* orig = (unsigned char*)malloc(size);
  unsigned char* buf = (unsigned char*)malloc(size);
  unsigned loaded = 0, rejected = 0, j;

  if (!orig || !buf)
    return 1;
  if (!serialize(orig, size))
  {
    fprintf(stderr, "saving a state failed\n");
    free(orig);
    free(buf);
    return 1;
  }
  if (!unserialize(orig, size))
  {
    fprintf(stderr, "loading a state just saved failed\n");
    free(orig);
    free(buf);
    return 1;
  }

  if (getenv("STATEFUZZ_DUMP"))
  {
    FILE* f = fopen(getenv("STATEFUZZ_DUMP"), "wb");
    if (f)
    {
      fwrite(orig, 1, size, f);
      fclose(f);
    }
  }

  rng_state = seed;
  signal(SIGALRM, alarm_handler);

  if (getenv("STATEFUZZ_PATCH"))
  {
    /* Replay one corruption: comma-separated offset:hexvalue pairs. */
    const char* p = getenv("STATEFUZZ_PATCH");
    memcpy(buf, orig, size);
    while (*p)
    {
      unsigned long off;
      unsigned val;
      int n = 0;
      if (sscanf(p, "%lu:%x%n", &off, &val, &n) != 2 || off >= size)
        break;
      buf[off] = (unsigned char)val;
      p += n;
      if (*p == ',')
        p++;
    }
    alarm(60);
    printf("patched state %s\n", unserialize(buf, size) ? "loaded" : "rejected");
    for (j = 0; j < 3; j++)
      run();
    alarm(0);
    iters = 0;
  }
  for (fuzz_iteration = 0; fuzz_iteration < iters; fuzz_iteration++)
  {
    corrupt_state(buf, orig, size);
    if (getenv("STATEFUZZ_LOG"))
    {
      size_t k;
      for (k = 0; k < size; k++)
      {
        if (buf[k] != orig[k])
          fprintf(stderr, "iteration %u: byte %lu %02x -> %02x\n", fuzz_iteration, (unsigned long)k, orig[k], buf[k]);
      }
    }
    alarm(60);
    if (unserialize(buf, size))
    {
      loaded++;
      for (j = 0; j < 3; j++)
        run();
    }
    else
    {
      rejected++;
    }
    alarm(0);

    if (!unserialize(orig, size))
    {
      fprintf(stderr, "restoring the good state failed at iteration %u\n", fuzz_iteration);
      return 1;
    }
  }

  printf("state fuzz: %u loaded, %u rejected\n", loaded, rejected);
  free(orig);
  free(buf);
  return 0;
}

int main(int argc, char** argv)
{
  void* h;
  unsigned frames, i;
  struct retro_game_info info;
  int rc = 0;

  if (argc < 3)
  {
    fprintf(stderr, "usage: frontend core.so frames [content]\n");
    return 1;
  }

  h = dlopen(argv[1], RTLD_NOW);
  if (!h)
  {
    fprintf(stderr, "%s\n", dlerror());
    return 1;
  }

  frames = (unsigned)atoi(argv[2]);
  if (getenv("FE_CPU"))
    opt_cpu = getenv("FE_CPU");
  if (getenv("FE_FASTMEM"))
    opt_fastmem = getenv("FE_FASTMEM");
  if (getenv("FE_GPUTHREAD"))
    opt_gpu_thread = getenv("FE_GPUTHREAD");

  ((fn_set_t)lookup(h, "retro_set_environment"))((void*)env_cb);
  ((fn_set_t)lookup(h, "retro_set_video_refresh"))((void*)video_cb);
  ((fn_set_t)lookup(h, "retro_set_audio_sample"))((void*)audio_cb);
  ((fn_set_t)lookup(h, "retro_set_audio_sample_batch"))((void*)audio_batch_cb);
  ((fn_set_t)lookup(h, "retro_set_input_poll"))((void*)input_poll_cb);
  ((fn_set_t)lookup(h, "retro_set_input_state"))((void*)input_state_cb);
  ((fn_void_t)lookup(h, "retro_init"))();

  memset(&info, 0, sizeof(info));
  info.path = (argc > 3) ? argv[3] : NULL;
  if (!((fn_load_game_t)lookup(h, "retro_load_game"))(info.path ? &info : NULL))
  {
    fprintf(stderr, "load failed\n");
    return 1;
  }

  for (i = 0; i < frames; i++)
  {
    /* Switch the threading options while running; the GPU thread keeps its setting at the end. */
    if (i == frames / 4)
    {
      opt_gpu_thread = "false";
      opt_readahead = "0";
      options_dirty = 1;
    }
    else if (i == frames / 2)
    {
      opt_gpu_thread = getenv("FE_GPUTHREAD") ? getenv("FE_GPUTHREAD") : "true";
      opt_readahead = "3";
      options_dirty = 1;
    }
    ((fn_void_t)lookup(h, "retro_run"))();
  }

  if (getenv("RAMDUMP"))
  {
    unsigned long offset = 0, count = 1, k;
    const unsigned char* ram =
      (const unsigned char*)((fn_memory_data_t)lookup(h, "retro_get_memory_data"))(RETRO_MEMORY_SYSTEM_RAM);
    if (sscanf(getenv("RAMDUMP"), "%li:%li", &offset, &count) < 1 || !ram)
    {
      fprintf(stderr, "bad RAMDUMP\n");
      return 1;
    }
    for (k = 0; k < count; k++)
    {
      uint32_t w;
      memcpy(&w, ram + offset + k * 4, 4);
      printf("%08lx %08x\n", offset + k * 4, (unsigned)w);
    }
  }

  if (getenv("STATEFUZZ"))
    rc = state_fuzz(h, (unsigned)atoi(getenv("STATEFUZZ")), getenv("SEED") ? (uint32_t)atoi(getenv("SEED")) : 1u);

  ((fn_void_t)lookup(h, "retro_unload_game"))();
  ((fn_void_t)lookup(h, "retro_deinit"))();
  /* No dlclose(): the core's static libstdc++ keeps a pool that would then show up as a leak. */
  return rc;
}
