/* trees.h — ordered-map tree variants + Huffman coder. */
#ifndef DISPATCH_TREES_H
#define DISPATCH_TREES_H

#include "dispatch/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ds_visitor_fn)(ds_key_t, ds_val_t, void* user);

/* ---- AVL ---- */
typedef struct tree_avl tree_avl_t;
DS_API tree_avl_t*  tree_avl_create(void);
DS_API void         tree_avl_destroy(tree_avl_t*);
DS_API ds_status_t  tree_avl_insert(tree_avl_t*, ds_key_t, ds_val_t);
DS_API ds_status_t  tree_avl_get(const tree_avl_t*, ds_key_t, ds_val_t* out);
DS_API ds_status_t  tree_avl_delete(tree_avl_t*, ds_key_t);
DS_API size_t       tree_avl_size(const tree_avl_t*);
DS_API void         tree_avl_inorder(const tree_avl_t*, ds_visitor_fn, void* user);

/* ---- Red-Black ---- */
typedef struct tree_rb tree_rb_t;
DS_API tree_rb_t*   tree_rb_create(void);
DS_API void         tree_rb_destroy(tree_rb_t*);
DS_API ds_status_t  tree_rb_insert(tree_rb_t*, ds_key_t, ds_val_t);
DS_API ds_status_t  tree_rb_get(const tree_rb_t*, ds_key_t, ds_val_t* out);
DS_API ds_status_t  tree_rb_delete(tree_rb_t*, ds_key_t);
DS_API size_t       tree_rb_size(const tree_rb_t*);
DS_API void         tree_rb_inorder(const tree_rb_t*, ds_visitor_fn, void* user);

/* ---- Splay ---- */
typedef struct tree_splay tree_splay_t;
DS_API tree_splay_t* tree_splay_create(void);
DS_API void          tree_splay_destroy(tree_splay_t*);
DS_API ds_status_t   tree_splay_insert(tree_splay_t*, ds_key_t, ds_val_t);
DS_API ds_status_t   tree_splay_get(tree_splay_t*, ds_key_t, ds_val_t* out);
DS_API ds_status_t   tree_splay_delete(tree_splay_t*, ds_key_t);
DS_API size_t        tree_splay_size(const tree_splay_t*);
DS_API void          tree_splay_inorder(const tree_splay_t*, ds_visitor_fn, void* user);

/* ---- B+ tree ---- */
typedef struct tree_bplus tree_bplus_t;
DS_API tree_bplus_t* tree_bplus_create(int order);
DS_API void          tree_bplus_destroy(tree_bplus_t*);
DS_API ds_status_t   tree_bplus_insert(tree_bplus_t*, ds_key_t, ds_val_t);
DS_API ds_status_t   tree_bplus_get(const tree_bplus_t*, ds_key_t, ds_val_t* out);
DS_API ds_status_t   tree_bplus_delete(tree_bplus_t*, ds_key_t);
DS_API size_t        tree_bplus_size(const tree_bplus_t*);
DS_API void          tree_bplus_inorder(const tree_bplus_t*, ds_visitor_fn, void* user);
DS_API void          tree_bplus_range(const tree_bplus_t*, ds_key_t lo, ds_key_t hi,
                                      ds_visitor_fn, void* user);

/* ---- Threaded BST ---- */
typedef struct tree_threaded tree_threaded_t;
DS_API tree_threaded_t* tree_threaded_create(void);
DS_API void             tree_threaded_destroy(tree_threaded_t*);
DS_API ds_status_t      tree_threaded_insert(tree_threaded_t*, ds_key_t, ds_val_t);
DS_API ds_status_t      tree_threaded_get(const tree_threaded_t*, ds_key_t, ds_val_t* out);
DS_API ds_status_t      tree_threaded_delete(tree_threaded_t*, ds_key_t);
DS_API size_t           tree_threaded_size(const tree_threaded_t*);
DS_API void             tree_threaded_inorder(const tree_threaded_t*, ds_visitor_fn, void*);

/* ---- Huffman ---- */
typedef struct huffman huffman_t;
DS_API huffman_t* huffman_build(const uint8_t* data, size_t n);
DS_API void       huffman_destroy(huffman_t*);
DS_API size_t     huffman_encode(const huffman_t*, const uint8_t* in, size_t in_n,
                                 uint8_t* out_bits, size_t out_cap);
DS_API size_t     huffman_decode(const huffman_t*, const uint8_t* in_bits, size_t bit_len,
                                 uint8_t* out, size_t out_cap);
DS_API double     huffman_ratio(const huffman_t*, const uint8_t* data, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* DISPATCH_TREES_H */
