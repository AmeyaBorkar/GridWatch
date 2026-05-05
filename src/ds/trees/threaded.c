/* threaded.c — right-threaded BST. If right_thread==1, the 'right' pointer is
 * the in-order successor rather than a child. Enables stack-free inorder. */
#include "dispatch/trees.h"
#include <stdlib.h>

/* THREADED BST NODE: a normal BST node plus a right_thread flag. When
 * right_thread is 1 the right pointer is reused as a "thread" pointing
 * directly to the in-order successor instead of a child. Used for replay
 * walks because it lets us iterate in order with no recursion or stack. */
typedef struct th_node {
    ds_key_t key;
    ds_val_t val;
    struct th_node* left;
    struct th_node* right;
    int right_thread; /* 1 => right is successor, not child */
} th_node_t;

struct tree_threaded {
    th_node_t* root;
    size_t count;
};

static th_node_t* node_new(ds_key_t k, ds_val_t v) {
    th_node_t* n = (th_node_t*)malloc(sizeof *n);
    if (!n) return NULL;
    n->key = k; n->val = v;
    n->left = NULL;
    n->right = NULL;
    n->right_thread = 1; /* no successor yet */
    return n;
}

static void destroy_rec(th_node_t* n) {
    if (!n) return;
    destroy_rec(n->left);
    if (!n->right_thread) destroy_rec(n->right);
    free(n);
}

tree_threaded_t* tree_threaded_create(void) {
    tree_threaded_t* t = (tree_threaded_t*)malloc(sizeof *t);
    if (!t) return NULL;
    t->root = NULL; t->count = 0;
    return t;
}

void tree_threaded_destroy(tree_threaded_t* t) {
    if (!t) return;
    destroy_rec(t->root);
    free(t);
}

ds_status_t tree_threaded_insert(tree_threaded_t* t, ds_key_t k, ds_val_t v) {
    if (!t) return DS_ERR_INVALID;
    if (!t->root) {
        th_node_t* n = node_new(k, v);
        if (!n) return DS_ERR_NOMEM;
        t->root = n;
        t->count = 1;
        return DS_OK;
    }
    th_node_t* cur = t->root;
    for (;;) {
        if (k == cur->key) return DS_ERR_DUP;
        if (k < cur->key) {
            if (cur->left) { cur = cur->left; continue; }
            th_node_t* n = node_new(k, v);
            if (!n) return DS_ERR_NOMEM;
            /* n's successor is cur */
            n->right = cur;
            n->right_thread = 1;
            cur->left = n;
            t->count++;
            return DS_OK;
        } else {
            if (!cur->right_thread) { cur = cur->right; continue; }
            th_node_t* n = node_new(k, v);
            if (!n) return DS_ERR_NOMEM;
            /* n inherits cur's old successor */
            n->right = cur->right;
            n->right_thread = 1;
            cur->right = n;
            cur->right_thread = 0;
            t->count++;
            return DS_OK;
        }
    }
}

ds_status_t tree_threaded_get(const tree_threaded_t* t, ds_key_t k, ds_val_t* out) {
    if (!t) return DS_ERR_INVALID;
    const th_node_t* cur = t->root;
    while (cur) {
        if (k == cur->key) { if (out) *out = cur->val; return DS_OK; }
        if (k < cur->key) cur = cur->left;
        else {
            if (cur->right_thread) break;
            cur = cur->right;
        }
    }
    return DS_ERR_NOT_FOUND;
}

/* Delete: locate, then handle cases. Thread maintenance is subtle. */
ds_status_t tree_threaded_delete(tree_threaded_t* t, ds_key_t k) {
    if (!t || !t->root) return DS_ERR_NOT_FOUND;
    th_node_t* parent = NULL;
    th_node_t* cur = t->root;
    int from_left = 0;
    while (cur && cur->key != k) {
        parent = cur;
        if (k < cur->key) { cur = cur->left; from_left = 1; }
        else {
            if (cur->right_thread) { cur = NULL; break; }
            cur = cur->right; from_left = 0;
        }
    }
    if (!cur) return DS_ERR_NOT_FOUND;

    /* For simplicity when two children: copy successor's data over, then
     * delete successor (which has at most one real child). */
    if (cur->left && !cur->right_thread) {
        /* find inorder successor: leftmost of right subtree */
        th_node_t* sp = cur;
        th_node_t* s = cur->right;
        int s_from_left = 0;
        while (s->left) { sp = s; s = s->left; s_from_left = 1; }
        cur->key = s->key;
        cur->val = s->val;
        cur = s;
        parent = sp;
        from_left = s_from_left;
    }

    /* Now cur has at most one real child. */
    th_node_t* child = NULL;
    int child_is_thread = 0;
    if (cur->left) {
        child = cur->left;
        /* fix thread of rightmost of left subtree (if it pointed back to cur
         * via thread, update to cur's successor). */
        th_node_t* rm = child;
        while (!rm->right_thread && rm->right) rm = rm->right;
        if (rm->right_thread) rm->right = cur->right_thread ? cur->right : cur->right;
    } else if (!cur->right_thread) {
        child = cur->right;
        /* fix thread of leftmost of right subtree if pointed at predecessor cur */
        th_node_t* lm = child;
        while (lm->left) lm = lm->left;
        (void)lm; /* left threads not stored; nothing to fix */
    } else {
        /* no children: replace with thread-to-successor from parent's POV */
        child = cur->right; /* successor */
        child_is_thread = 1;
    }

    if (!parent) {
        if (child_is_thread) t->root = NULL;
        else t->root = child;
    } else if (from_left) {
        parent->left = child_is_thread ? NULL : child;
    } else {
        if (child_is_thread) {
            parent->right = child; /* successor */
            parent->right_thread = 1;
        } else {
            parent->right = child;
            parent->right_thread = 0;
        }
    }
    free(cur);
    t->count--;
    return DS_OK;
}

size_t tree_threaded_size(const tree_threaded_t* t) { return t ? t->count : 0; }

static const th_node_t* leftmost(const th_node_t* n) {
    if (!n) return NULL;
    while (n->left) n = n->left;
    return n;
}

/* THREADED IN-ORDER TRAVERSAL — STACK-FREE: start at the leftmost node, then
 * at each step either follow the right thread directly to the successor (when
 * right_thread is set) or descend to the leftmost of the right subtree. No
 * recursion, no explicit stack — that's the whole point of threading. */
void tree_threaded_inorder(const tree_threaded_t* t, ds_visitor_fn fn, void* user) {
    if (!t || !fn) return;
    const th_node_t* cur = leftmost(t->root);
    while (cur) {
        fn(cur->key, cur->val, user);
        if (cur->right_thread) cur = cur->right;
        else cur = leftmost(cur->right);
    }
}
