/* Test helper: TREXIO files of the in-memory back end. */
#ifndef TV_MEMORY_HELPER_H
#define TV_MEMORY_HELPER_H

#include <stdint.h>

#include <trexio.h>

#if defined(_WIN32)
#  define TV_MEMORY_API __declspec(dllexport)
#else
#  define TV_MEMORY_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TV_MEMORY_INTACT 0
#define TV_MEMORY_BAD_NUCLEUS_REPULSION 1
#define TV_MEMORY_BAD_MO_COEFFICIENT 2

/* 1 if TREXIO has the in-memory back end, 0 if not. */
TV_MEMORY_API int tv_memory_available(void);
/* Copy of the TREXIO file at path in a new TREXIO_MEMORY file, with the given
   corruption, or NULL on error. */
TV_MEMORY_API trexio_t* tv_memory_copy(const char* path, int corruption);
TV_MEMORY_API void tv_memory_close(trexio_t* file);
/* nucleus.num of the file, to check that it is still usable; -1 on error. */
TV_MEMORY_API int32_t tv_memory_nucleus_num(trexio_t* file);

#ifdef __cplusplus
}
#endif

#endif
