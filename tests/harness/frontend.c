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
 *   DISKTEST=path         content is a two-entry M3U booted on its second entry;
 *                         swap, load a state saved before the swap, remove and
 *                         re-add entries through disk control, re-adding path,
 *                         close the tray on an empty drive and load that state,
 *                         then query its path and label with bad buffers
 *   RELOADTEST=1          after the run, re-init and load the content twice more
 *                         under a new environment: once with a second logger,
 *                         once with a NULL one; fails if a logger is called
 *                         while it is not the current one
 * Core options come from FE_CPU, FE_FASTMEM and FE_GPUTHREAD; the BIOS
 * and save directory is FE_DIR. A load that runs for more than a minute
 * is reported as a hang (exit code 3). */
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"

#ifdef _WIN32
#include <windows.h>
#define dlopen(path, flags) ((void*)LoadLibraryA(path))
#define dlsym(h, name) ((void*)GetProcAddress((HMODULE)(h), name))
#define dlerror() "LoadLibrary failed"
#define alarm(s) ((void)0)
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

static const char* opt_gpu_thread = "true";
static const char* opt_readahead = "8";
static const char* opt_fastmem = "MMap";
static const char* opt_cpu = "Recompiler";
static int options_dirty = 0;
static struct retro_disk_control_ext_callback disk_cb;
static int have_disk_cb = 0;

/* 0: log_cb, 1: log_cb_second, 2: a NULL logger */
static int logger_phase = 0;
static unsigned stale_log_calls = 0;
static unsigned second_log_calls = 0;

static void log_cb(enum retro_log_level level, const char* fmt, ...)
{
  va_list ap;
  if (logger_phase != 0)
    stale_log_calls++;
  if (level < RETRO_LOG_WARN && !getenv("VERBOSE"))
    return;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
}

static void log_cb_second(enum retro_log_level level, const char* fmt, ...)
{
  (void)level;
  (void)fmt;
  if (logger_phase != 1)
    stale_log_calls++;
  else
    second_log_calls++;
}

static bool env_cb(unsigned cmd, void* data)
{
  switch (cmd)
  {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      if (logger_phase == 0)
        ((struct retro_log_callback*)data)->log = log_cb;
      else if (logger_phase == 1)
        ((struct retro_log_callback*)data)->log = log_cb_second;
      else
        ((struct retro_log_callback*)data)->log = NULL;
      return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char**)data = getenv("FE_DIR");
      return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
      return true;
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
      *(unsigned*)data = 1;
      return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
      disk_cb = *(const struct retro_disk_control_ext_callback*)data;
      have_disk_cb = 1;
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
#ifndef _WIN32
static void alarm_handler(int sig)
{
  (void)sig;
  fprintf(stderr, "HANG at state fuzz iteration %u\n", fuzz_iteration);
  _exit(3);
}
#endif

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
    const size_t mlen = strlen(markers[mi]);
    size_t off = 0, p;
    unsigned found = 0;
    unsigned char v;

    if (getenv("STATEFUZZ_SECTION") && strcmp(getenv("STATEFUZZ_SECTION"), markers[mi]) != 0)
      continue;

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
#ifndef _WIN32
  signal(SIGALRM, alarm_handler);
#endif

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

#define DISK_CHECK(cond)                                    \
  do                                                        \
  {                                                         \
    if (!(cond))                                            \
    {                                                       \
      fprintf(stderr, "disk test: line %d failed: %s\n", __LINE__, #cond); \
      free(state);                                          \
      return 1;                                             \
    }                                                       \
  } while (0)

static void run_frames(fn_void_t run, unsigned n)
{
  unsigned i;
  for (i = 0; i < n; i++)
    run();
}

static int disk_test(void* h, const char* readd_path)
{
  fn_void_t run = (fn_void_t)lookup(h, "retro_run");
  fn_serialize_size_t serialize_size = (fn_serialize_size_t)lookup(h, "retro_serialize_size");
  fn_serialize_t serialize = (fn_serialize_t)lookup(h, "retro_serialize");
  fn_unserialize_t unserialize = (fn_unserialize_t)lookup(h, "retro_unserialize");
  size_t size = serialize_size();
  /* Two states back to back: the boot state, then one with an empty closed tray. */
  unsigned char* state = (unsigned char*)malloc(size * 2);
  unsigned char* empty_state = state ? state + size : NULL;
  struct retro_game_info disc;
  char buf[1024];
  const size_t readd_len = strlen(readd_path);

  DISK_CHECK(state != NULL);
  DISK_CHECK(have_disk_cb);
  DISK_CHECK(disk_cb.get_num_images() == 2);
  DISK_CHECK(disk_cb.get_image_index() == 1);
  DISK_CHECK(!disk_cb.get_eject_state());

  /* The state names the playlist and its second entry. */
  DISK_CHECK(serialize(state, size));

  /* The first entry goes in as an image of its own. */
  DISK_CHECK(disk_cb.set_eject_state(true));
  DISK_CHECK(disk_cb.set_image_index(0));
  DISK_CHECK(disk_cb.set_eject_state(false));
  run_frames(run, 3);

  DISK_CHECK(unserialize(state, size));
  run_frames(run, 3);

  /* Removing the selected last entry leaves index == count: no disc, and
   * the tray closes on an empty drive. */
  DISK_CHECK(!disk_cb.get_eject_state());
  DISK_CHECK(disk_cb.set_eject_state(true));
  DISK_CHECK(disk_cb.get_eject_state());
  DISK_CHECK(disk_cb.set_image_index(1));
  DISK_CHECK(disk_cb.replace_image_index(1, NULL));
  DISK_CHECK(disk_cb.get_num_images() == 1);
  DISK_CHECK(disk_cb.get_image_index() == 1);
  DISK_CHECK(disk_cb.set_eject_state(false));
  DISK_CHECK(!disk_cb.get_eject_state());
  run_frames(run, 3);

  /* The closed empty tray is part of the state, so loading it closes an open tray. */
  DISK_CHECK(serialize(empty_state, size));
  DISK_CHECK(disk_cb.set_eject_state(true));
  DISK_CHECK(unserialize(empty_state, size));
  DISK_CHECK(!disk_cb.get_eject_state());
  run_frames(run, 3);

  /* Entries are edited with the tray open. */
  DISK_CHECK(!disk_cb.replace_image_index(0, NULL));
  DISK_CHECK(disk_cb.set_eject_state(true));
  DISK_CHECK(disk_cb.replace_image_index(0, NULL));
  DISK_CHECK(disk_cb.get_num_images() == 0);
  DISK_CHECK(disk_cb.set_eject_state(false));
  DISK_CHECK(disk_cb.set_eject_state(true));

  /* An added slot keeps no disc selected and cannot be selected until it is replaced. */
  DISK_CHECK(disk_cb.add_image_index());
  DISK_CHECK(disk_cb.get_image_index() == 1);
  DISK_CHECK(!disk_cb.set_image_index(0));

  memset(&disc, 0, sizeof(disc));
  disc.path = readd_path;
  DISK_CHECK(disk_cb.replace_image_index(0, &disc));
  DISK_CHECK(disk_cb.set_image_index(0));
  DISK_CHECK(disk_cb.set_eject_state(false));
  DISK_CHECK(!disk_cb.get_eject_state());
  run_frames(run, 3);

  /* Path and label buffers: missing, empty and short ones fail; a path
   * never comes back truncated, a label may. */
  DISK_CHECK(readd_len < sizeof(buf));
  DISK_CHECK(!disk_cb.get_image_path(0, NULL, sizeof(buf)));
  DISK_CHECK(!disk_cb.get_image_path(0, buf, 0));
  DISK_CHECK(!disk_cb.get_image_path(0, buf, readd_len));
  DISK_CHECK(disk_cb.get_image_path(0, buf, readd_len + 1) && !strcmp(buf, readd_path));
  DISK_CHECK(!disk_cb.get_image_path(1, buf, sizeof(buf)));
  DISK_CHECK(!disk_cb.get_image_label(0, NULL, sizeof(buf)));
  DISK_CHECK(!disk_cb.get_image_label(0, buf, 0));
  DISK_CHECK(disk_cb.get_image_label(0, buf, 2) && strlen(buf) == 1);

  printf("disk test: passed\n");
  free(state);
  return 0;
}

static int reload_test(void* h, const char* content)
{
  struct retro_game_info info;
  int phase;

  memset(&info, 0, sizeof(info));
  info.path = content;
  for (phase = 1; phase <= 2; phase++)
  {
    logger_phase = phase;
    ((fn_set_t)lookup(h, "retro_set_environment"))((void*)env_cb);
    ((fn_void_t)lookup(h, "retro_init"))();
    if (!((fn_load_game_t)lookup(h, "retro_load_game"))(content ? &info : NULL))
    {
      fprintf(stderr, "reload test: load %d failed\n", phase);
      return 1;
    }
    run_frames((fn_void_t)lookup(h, "retro_run"), 30);
    ((fn_void_t)lookup(h, "retro_unload_game"))();
    ((fn_void_t)lookup(h, "retro_deinit"))();
  }

  if (stale_log_calls || !second_log_calls)
  {
    fprintf(stderr, "reload test: failed: %u calls to a replaced logger, %u to the current one\n", stale_log_calls,
            second_log_calls);
    return 1;
  }
  printf("reload test: passed (%u log calls after the reload)\n", second_log_calls);
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
  if (getenv("DISKTEST"))
  {
    if (argc < 4 || !have_disk_cb || !disk_cb.set_initial_image(1, argv[3]))
    {
      fprintf(stderr, "disk test: no disk control interface\n");
      return 1;
    }
  }

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

  if (getenv("DISKTEST"))
    rc = disk_test(h, getenv("DISKTEST"));
  if (!rc && getenv("STATEFUZZ"))
    rc = state_fuzz(h, (unsigned)atoi(getenv("STATEFUZZ")), getenv("SEED") ? (uint32_t)atoi(getenv("SEED")) : 1u);

  ((fn_void_t)lookup(h, "retro_unload_game"))();
  ((fn_void_t)lookup(h, "retro_deinit"))();
  if (!rc && getenv("RELOADTEST"))
    rc = reload_test(h, info.path);
  /* No dlclose(): the core's static libstdc++ keeps a pool that would then show up as a leak. */
  return rc;
}
