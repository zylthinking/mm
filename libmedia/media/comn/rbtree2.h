/*
  Red Black Trees
  (C) 1999  Andrea Arcangeli <andrea@suse.de>
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  linux/include/linux/rbtree.h

  To use rbtrees you'll have to implement your own insert and search cores.
  This will avoid us to use callbacks and to drop drammatically performances.
  I know it's not the cleaner way,  but in C (not in C++) to get
  performances and genericity...

  See Documentation/rbtree.txt for documentation and samples.
*/

#ifndef _LINUX_RBTREE_H
#define _LINUX_RBTREE_H

#include <stddef.h>

#ifndef container_of
#define container_of(ptr, type, member) ({   \
    const typeof( ((type *)0)->member ) *__mptr = (ptr); \
    (type *)( (char *)__mptr - offsetof(type,member) ); \
})
#endif

struct rb_node2 {
    unsigned long  __rb_parent_color;
    struct rb_node2 *rb_right;
    struct rb_node2 *rb_left;
} __attribute__((aligned(sizeof(long))));
/* The alignment might seem pointless, but allegedly CRIS needs it */

struct rb_root {
    struct rb_node2 *rb_node2;
};


#define rb_parent(r)   ((struct rb_node2 *)((r)->__rb_parent_color & ~3))

#define RB_ROOT (struct rb_root) { NULL, }
#define rb_entry(ptr, type, member) container_of(ptr, type, member)

#define RB_EMPTY_ROOT(root)  ((root)->rb_node2 == NULL)

/* 'empty' nodes are nodes that are known not to be inserted in an rbree */
#define RB_EMPTY_NODE(node)  \
    ((node)->__rb_parent_color == (unsigned long)(node))
#define RB_CLEAR_NODE(node)  \
    ((node)->__rb_parent_color = (unsigned long)(node))


extern void rb_insert_color(struct rb_node2 *, struct rb_root *);
extern void rb_erase(struct rb_node2 *, struct rb_root *);


/* Find logical next and previous nodes in a tree */
extern struct rb_node2 *rb_next(const struct rb_node2 *);
extern struct rb_node2 *rb_prev(const struct rb_node2 *);
extern struct rb_node2 *rb_first(const struct rb_root *);
extern struct rb_node2 *rb_last(const struct rb_root *);

/* Fast replacement of a single node without remove/rebalance/add/rebalance */
extern void rb_replace_node(struct rb_node2 *victim, struct rb_node2 *new, 
                struct rb_root *root);

static inline void rb_link_node(struct rb_node2 * node, struct rb_node2 * parent,
                struct rb_node2 ** rb_link)
{
    node->__rb_parent_color = (unsigned long)parent;
    node->rb_left = node->rb_right = NULL;

    *rb_link = node;
}

#define rb_find(root, father, slot, np, field, x)       \
({                                                      \
    father = (root);                                    \
    slot = &(root);                                     \
    typeof(*(np))* ref = NULL;                          \
                                                        \
    while (*slot) {                                     \
        father = *slot;                                 \
        ref = rb_entry(father, typeof(*(np)), field);   \
        int n = x((np), ref);                           \
        if (n == 0) {                                   \
            break;                                      \
        }                                               \
                                                        \
        if (n < 0) {                                    \
            slot = &(father->rb_left);                  \
        } else {                                        \
            slot = &(father->rb_right);                 \
        }                                               \
    }                                                   \
    ref;                                                \
})

#endif  /* _LINUX_RBTREE_H */
