/* spatial.h — spatial data structures for libdispatch. */
#ifndef DISPATCH_SPATIAL_H
#define DISPATCH_SPATIAL_H

#include "dispatch/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { double x, y; ds_val_t data; } sp_point_t;
typedef struct { double x1, y1, x2, y2; ds_val_t data; } sp_rect_t;

/* ---------------- Quadtree ---------------- */
typedef struct sp_quad sp_quad_t;

DS_API sp_quad_t*  sp_quad_create(double x, double y, double w, double h);
DS_API void        sp_quad_destroy(sp_quad_t*);
DS_API ds_status_t sp_quad_insert(sp_quad_t*, double x, double y, ds_val_t);
DS_API size_t      sp_quad_query(const sp_quad_t*, double x, double y, double w, double h, sp_point_t* out, size_t max);
DS_API size_t      sp_quad_nearest(const sp_quad_t*, double x, double y, size_t k, sp_point_t* out);

/* ---------------- KD-tree ---------------- */
typedef struct sp_kd sp_kd_t;

DS_API sp_kd_t*    sp_kd_build(const sp_point_t* pts, size_t n);
DS_API void        sp_kd_destroy(sp_kd_t*);
DS_API ds_status_t sp_kd_insert(sp_kd_t*, double x, double y, ds_val_t);
DS_API size_t      sp_kd_nearest(const sp_kd_t*, double x, double y, size_t k, sp_point_t* out);

/* ---------------- R-tree ---------------- */
typedef struct sp_rtree sp_rtree_t;

DS_API sp_rtree_t* sp_rtree_create(void);
DS_API void        sp_rtree_destroy(sp_rtree_t*);
DS_API ds_status_t sp_rtree_insert(sp_rtree_t*, double x1, double y1, double x2, double y2, ds_val_t);
DS_API size_t      sp_rtree_search(const sp_rtree_t*, double x1, double y1, double x2, double y2, sp_rect_t* out, size_t max);

/* ---------------- Segment tree ---------------- */
typedef struct sp_seg sp_seg_t;

DS_API sp_seg_t*  sp_seg_create(size_t n);
DS_API void       sp_seg_destroy(sp_seg_t*);
DS_API void       sp_seg_update(sp_seg_t*, size_t idx, long long value);
DS_API long long  sp_seg_query(const sp_seg_t*, size_t lo, size_t hi);

/* ---------------- Interval tree ---------------- */
typedef struct sp_itree sp_itree_t;

DS_API sp_itree_t* sp_itree_create(void);
DS_API void        sp_itree_destroy(sp_itree_t*);
DS_API ds_status_t sp_itree_insert(sp_itree_t*, double lo, double hi, ds_val_t);
DS_API size_t      sp_itree_stab(const sp_itree_t*, double p, sp_rect_t* out, size_t max);

/* ---------------- Range tree ---------------- */
typedef struct sp_range sp_range_t;

DS_API sp_range_t* sp_range_build(const sp_point_t* pts, size_t n);
DS_API void        sp_range_destroy(sp_range_t*);
DS_API size_t      sp_range_search(const sp_range_t*, double x1, double y1, double x2, double y2, sp_point_t* out, size_t max);

/* ---------------- BSP tree ---------------- */
typedef struct sp_bsp sp_bsp_t;

DS_API sp_bsp_t* sp_bsp_build(const sp_point_t* pts, size_t n);
DS_API void      sp_bsp_destroy(sp_bsp_t*);
DS_API int       sp_bsp_depth(const sp_bsp_t*);
DS_API size_t    sp_bsp_region(const sp_bsp_t*, double x, double y, sp_point_t* out, size_t max);

#ifdef __cplusplus
}
#endif

#endif /* DISPATCH_SPATIAL_H */
