#ifndef __CORETYPES_H__
#define __CORETYPES_H__

#include <stdint.h>
#include <stdio.h>

#ifdef __LIBRETRO__
#include <streams/file_stream.h>
#endif

#define ARRAY_LENGTH(x) (sizeof(x)/sizeof(x[0]))

#if defined(__PS3__) || defined(__PSL1GHT__)
#undef UINT32
#undef UINT16
#undef UINT8
#undef INT32
#undef INT16
#undef INT8
#endif

typedef uint64_t UINT64;
typedef uint32_t UINT32;
typedef uint16_t UINT16;
typedef uint8_t UINT8;

typedef int64_t INT64;
typedef int32_t INT32;
typedef int16_t INT16;
typedef int8_t INT8;

#ifdef __LIBRETRO__
#define core_file RFILE
#define core_fopen(file) rfopen(file, "rb")
#define core_fseek rfseek
#define core_ftell rftell
#define core_fread(fc, buff, len) rfread(buff, 1, len, fc)
#define core_fclose rfclose

#ifdef __cplusplus
extern "C" {
#endif

RFILE* rfopen(const char *path, const char *mode);
extern int64_t rfseek(RFILE* stream, int64_t offset, int origin);
extern int64_t rftell(RFILE* stream);
extern int64_t rfread(void* buffer,
   size_t elem_size, size_t elem_count, RFILE* stream);
extern int rfclose(RFILE* stream);

#ifdef __cplusplus
}
#endif

static INLINE UINT64 core_fsize(core_file *f)
{
    UINT64 rv;
    UINT64 p = core_ftell(f);
    core_fseek(f, 0, SEEK_END);
    rv = core_ftell(f);
    core_fseek(f, p, SEEK_SET);
    return rv;
}
#endif

#endif
