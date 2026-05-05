/* common.h — shared types and error codes for the dispatch library.
 *
 * Stable ABI: usable from C, C++, Python (ctypes/cffi), WASM, etc.
 *
 * The whole file is the public face of the opaque-handle pattern:
 * fixed POD types + status enum + DS_API export macro, with no struct
 * internals exposed. That's what lets the same compiled DLL be loaded
 * from a TUI, a Python binding, or a WASM build without recompiling. */
#ifndef DISPATCH_COMMON_H
#define DISPATCH_COMMON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DS_API: build-mode visibility macro. Expands to dllexport when building the
 * shared lib, dllimport when consuming it, and nothing for static/WASM builds.
 * Lets one set of headers serve all three deployment targets. */
#if defined(_WIN32) && defined(DISPATCH_BUILD_SHARED)
#  define DS_API __declspec(dllexport)
#elif defined(_WIN32) && defined(DISPATCH_USE_SHARED)
#  define DS_API __declspec(dllimport)
#else
#  define DS_API
#endif

/* Generic key/value pair carried by every container. ds_val_t = void* is the
 * classic opaque-payload trick: the library stores user pointers without
 * knowing their type, so the same heap/tree code serves all callers. */
typedef double   ds_key_t;
typedef void*    ds_val_t;

/* Uniform status code returned by every public API. Lets callers in any
 * language check for success/failure via a single integer convention. */
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
