/* common.h — shared types and error codes for the dispatch library.
 *
 * Stable ABI: usable from C, C++, Python (ctypes/cffi), WASM, etc.
 */
#ifndef DISPATCH_COMMON_H
#define DISPATCH_COMMON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(DISPATCH_BUILD_SHARED)
#  define DS_API __declspec(dllexport)
#elif defined(_WIN32) && defined(DISPATCH_USE_SHARED)
#  define DS_API __declspec(dllimport)
#else
#  define DS_API
#endif

typedef double   ds_key_t;
typedef void*    ds_val_t;

typedef enum {
    DS_OK = 0,
    DS_ERR_NOMEM = -1,
    DS_ERR_EMPTY = -2,
    DS_ERR_NOT_FOUND = -3,
    DS_ERR_INVALID = -4,
    DS_ERR_DUP = -5
} ds_status_t;

typedef struct {
    ds_key_t key;
    ds_val_t val;
} ds_entry_t;

#ifdef __cplusplus
}
#endif

#endif
