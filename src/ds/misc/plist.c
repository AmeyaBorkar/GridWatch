/* plist.c — persistent cons list; parent owns all nodes. */
#include "dispatch/misc.h"
#include <stdlib.h>

struct misc_pnode {
    ds_val_t val;
    struct misc_pnode* tail;
    size_t   len;
};

struct misc_plist {
    misc_pnode_t** nodes;
    size_t count;
    size_t cap;
};

DS_API misc_plist_t* misc_plist_create(void) {
    misc_plist_t* p = (misc_plist_t*)malloc(sizeof(*p));
    if (!p) return NULL;
    p->nodes = NULL;
    p->count = 0;
    p->cap = 0;
    return p;
}

DS_API void misc_plist_destroy(misc_plist_t* p) {
    if (!p) return;
    for (size_t i = 0; i < p->count; i++) free(p->nodes[i]);
    free(p->nodes);
    free(p);
}

DS_API misc_pnode_t* misc_plist_push(misc_plist_t* p, misc_pnode_t* head, ds_val_t v) {
    if (!p) return NULL;
    if (p->count == p->cap) {
        size_t nc = p->cap ? p->cap * 2 : 16;
        misc_pnode_t** nn = (misc_pnode_t**)realloc(p->nodes, sizeof(*nn) * nc);
        if (!nn) return NULL;
        p->nodes = nn;
        p->cap = nc;
    }
    misc_pnode_t* node = (misc_pnode_t*)malloc(sizeof(*node));
    if (!node) return NULL;
    node->val = v;
    node->tail = head;
    node->len = head ? head->len + 1 : 1;
    p->nodes[p->count++] = node;
    return node;
}

DS_API ds_status_t misc_plist_head(const misc_pnode_t* n, ds_val_t* out) {
    if (!n) return DS_ERR_EMPTY;
    if (out) *out = n->val;
    return DS_OK;
}

DS_API misc_pnode_t* misc_plist_tail(const misc_pnode_t* n) {
    return n ? n->tail : NULL;
}

DS_API size_t misc_plist_length(const misc_pnode_t* n) {
    return n ? n->len : 0;
}
