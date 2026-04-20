/* test_spatial.c — tests for spatial module. */
#include "dispatch/spatial.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#define N_Q 500
#define N_KD 500
#define N_RT 100
#define N_IT 50

static double frand(double lo, double hi) {
    return lo + (hi - lo) * ((double)rand() / (double)RAND_MAX);
}

static int pt_cmp_d2(const void* a, const void* b) {
    double da = *(const double*)a, db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

static int test_quad(void) {
    sp_quad_t* q = sp_quad_create(0, 0, 1000, 1000);
    assert(q);
    static sp_point_t pts[N_Q];
    for (int i = 0; i < N_Q; i++) {
        pts[i].x = frand(0, 1000);
        pts[i].y = frand(0, 1000);
        pts[i].data = (void*)(intptr_t)i;
        ds_status_t s = sp_quad_insert(q, pts[i].x, pts[i].y, pts[i].data);
        assert(s == DS_OK);
    }
    /* Range query center 200x200: from (400,400) to (600,600). */
    sp_point_t out[N_Q];
    size_t got = sp_quad_query(q, 400, 400, 200, 200, out, N_Q);
    size_t expect = 0;
    for (int i = 0; i < N_Q; i++) {
        if (pts[i].x >= 400 && pts[i].x <= 600 && pts[i].y >= 400 && pts[i].y <= 600) expect++;
    }
    if (got != expect) {
        fprintf(stderr, "quad_query: got %zu, expected %zu\n", got, expect);
        return 1;
    }
    /* Nearest k=5 match brute force. */
    double qx = 500, qy = 500;
    sp_point_t near[5];
    size_t nn = sp_quad_nearest(q, qx, qy, 5, near);
    assert(nn == 5);
    /* Brute force top-5 distances. */
    double d2[N_Q];
    for (int i = 0; i < N_Q; i++) {
        double dx = pts[i].x - qx, dy = pts[i].y - qy;
        d2[i] = dx*dx + dy*dy;
    }
    qsort(d2, N_Q, sizeof(double), pt_cmp_d2);
    for (int i = 0; i < 5; i++) {
        double dx = near[i].x - qx, dy = near[i].y - qy;
        double nd2 = dx*dx + dy*dy;
        if (fabs(nd2 - d2[i]) > 1e-9) {
            fprintf(stderr, "quad_nearest[%d] d2=%g brute=%g\n", i, nd2, d2[i]);
            return 1;
        }
    }
    sp_quad_destroy(q);
    return 0;
}

static int test_kd(void) {
    static sp_point_t pts[N_KD];
    for (int i = 0; i < N_KD; i++) {
        pts[i].x = frand(0, 1000);
        pts[i].y = frand(0, 1000);
        pts[i].data = (void*)(intptr_t)i;
    }
    sp_kd_t* t = sp_kd_build(pts, N_KD);
    assert(t);
    double qx = 123, qy = 456;
    sp_point_t near[3];
    size_t nn = sp_kd_nearest(t, qx, qy, 3, near);
    assert(nn == 3);
    double d2[N_KD];
    for (int i = 0; i < N_KD; i++) {
        double dx = pts[i].x - qx, dy = pts[i].y - qy;
        d2[i] = dx*dx + dy*dy;
    }
    qsort(d2, N_KD, sizeof(double), pt_cmp_d2);
    for (int i = 0; i < 3; i++) {
        double dx = near[i].x - qx, dy = near[i].y - qy;
        double nd2 = dx*dx + dy*dy;
        if (fabs(nd2 - d2[i]) > 1e-9) {
            fprintf(stderr, "kd_nearest[%d] d2=%g brute=%g\n", i, nd2, d2[i]);
            return 1;
        }
    }
    sp_kd_destroy(t);
    return 0;
}

static int test_rtree(void) {
    sp_rtree_t* t = sp_rtree_create();
    assert(t);
    double x1[N_RT], y1[N_RT], x2[N_RT], y2[N_RT];
    for (int i = 0; i < N_RT; i++) {
        x1[i] = frand(0, 900);
        y1[i] = frand(0, 900);
        x2[i] = x1[i] + frand(1, 100);
        y2[i] = y1[i] + frand(1, 100);
        ds_status_t s = sp_rtree_insert(t, x1[i], y1[i], x2[i], y2[i], (void*)(intptr_t)i);
        assert(s == DS_OK);
    }
    double px1 = 400, py1 = 400, px2 = 600, py2 = 600;
    sp_rect_t out[N_RT];
    size_t got = sp_rtree_search(t, px1, py1, px2, py2, out, N_RT);
    size_t expect = 0;
    for (int i = 0; i < N_RT; i++) {
        if (!(x2[i] < px1 || px2 < x1[i] || y2[i] < py1 || py2 < y1[i])) expect++;
    }
    if (got != expect) {
        fprintf(stderr, "rtree_search: got %zu, expected %zu\n", got, expect);
        return 1;
    }
    sp_rtree_destroy(t);
    return 0;
}

static int test_seg(void) {
    size_t n = 100;
    sp_seg_t* s = sp_seg_create(n);
    assert(s);
    long long arr[100] = {0};
    for (int i = 0; i < 50; i++) {
        size_t idx = rand() % n;
        long long v = rand() % 1000;
        arr[idx] = v;
        sp_seg_update(s, idx, v);
    }
    /* Verify random ranges. */
    for (int t = 0; t < 20; t++) {
        size_t lo = rand() % n;
        size_t hi = lo + rand() % (n - lo);
        long long sum = 0;
        for (size_t i = lo; i <= hi; i++) sum += arr[i];
        long long got = sp_seg_query(s, lo, hi);
        if (got != sum) {
            fprintf(stderr, "seg: [%zu,%zu] got %lld expected %lld\n", lo, hi, got, sum);
            return 1;
        }
    }
    sp_seg_destroy(s);
    return 0;
}

static int test_itree(void) {
    sp_itree_t* t = sp_itree_create();
    assert(t);
    double lo[N_IT], hi[N_IT];
    for (int i = 0; i < N_IT; i++) {
        lo[i] = frand(0, 900);
        hi[i] = lo[i] + frand(1, 100);
        ds_status_t s = sp_itree_insert(t, lo[i], hi[i], (void*)(intptr_t)i);
        assert(s == DS_OK);
    }
    for (int k = 0; k < 5; k++) {
        double p = frand(0, 1000);
        sp_rect_t out[N_IT];
        size_t got = sp_itree_stab(t, p, out, N_IT);
        size_t expect = 0;
        for (int i = 0; i < N_IT; i++) {
            if (lo[i] <= p && p <= hi[i]) expect++;
        }
        if (got != expect) {
            fprintf(stderr, "itree_stab p=%g got %zu expected %zu\n", p, got, expect);
            return 1;
        }
    }
    sp_itree_destroy(t);
    return 0;
}

static int test_range(void) {
    size_t n = 200;
    sp_point_t* pts = (sp_point_t*)malloc(sizeof(sp_point_t) * n);
    for (size_t i = 0; i < n; i++) {
        pts[i].x = frand(0, 1000);
        pts[i].y = frand(0, 1000);
        pts[i].data = (void*)(intptr_t)i;
    }
    sp_range_t* t = sp_range_build(pts, n);
    assert(t);
    double x1 = 200, y1 = 300, x2 = 700, y2 = 800;
    sp_point_t out[300];
    size_t got = sp_range_search(t, x1, y1, x2, y2, out, 300);
    size_t expect = 0;
    for (size_t i = 0; i < n; i++) {
        if (pts[i].x >= x1 && pts[i].x <= x2 && pts[i].y >= y1 && pts[i].y <= y2) expect++;
    }
    if (got != expect) {
        fprintf(stderr, "range_search: got %zu expected %zu\n", got, expect);
        free(pts);
        return 1;
    }
    sp_range_destroy(t);
    free(pts);
    return 0;
}

static int test_bsp(void) {
    size_t n = 100;
    sp_point_t* pts = (sp_point_t*)malloc(sizeof(sp_point_t) * n);
    for (size_t i = 0; i < n; i++) {
        pts[i].x = frand(0, 1000);
        pts[i].y = frand(0, 1000);
        pts[i].data = (void*)(intptr_t)i;
    }
    sp_bsp_t* t = sp_bsp_build(pts, n);
    assert(t);
    int d = sp_bsp_depth(t);
    if (d <= 0) {
        fprintf(stderr, "bsp depth %d\n", d);
        free(pts);
        return 1;
    }
    sp_point_t out[32];
    size_t got = sp_bsp_region(t, 500, 500, out, 32);
    (void)got;
    sp_bsp_destroy(t);
    free(pts);
    return 0;
}

int main(void) {
    srand(42);
    int rc = 0;
    rc |= test_quad();    printf("quad: %s\n", rc ? "FAIL" : "ok");
    rc |= test_kd();      printf("kd: %s\n", rc ? "FAIL" : "ok");
    rc |= test_rtree();   printf("rtree: %s\n", rc ? "FAIL" : "ok");
    rc |= test_seg();     printf("seg: %s\n", rc ? "FAIL" : "ok");
    rc |= test_itree();   printf("itree: %s\n", rc ? "FAIL" : "ok");
    rc |= test_range();   printf("range: %s\n", rc ? "FAIL" : "ok");
    rc |= test_bsp();     printf("bsp: %s\n", rc ? "FAIL" : "ok");
    return rc ? 1 : 0;
}
