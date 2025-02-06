
#ifndef my_buffer_h
#define my_buffer_h

#include "mydef.h"
#include "list_head.h"
#include <stdint.h>

struct mem_head;
typedef intptr_t (*addref_t)(struct mem_head *, intptr_t);

struct mem_head {
    union {
        struct list_head head;
        struct {
            intptr_t ref;
            addref_t addref;
        } mem;
    } u;
};

struct mbuf_operations {
    struct my_buffer* (*clone) (struct my_buffer*);
    void (*free) (struct my_buffer*);
};

struct my_buffer {
    struct list_head head;
    struct mem_head* mem_head;
    struct mbuf_operations* mop;

    void* any;
    char* ptr[2];
    uintptr_t length;
};

#define my_buffer_refcount(mbuf) \
({ \
    intptr_t n = 1; \
    if (mbuf->mem_head != NULL) { \
        n = mbuf->u.mem.ref; \
    } \
    n; \
})

static inline uint32_t free_buffer(struct list_head* head)
{
    uint32_t n = 0;
    while (!list_empty(head)) {
        struct list_head* ent = head->next;
        list_del(ent);

        struct my_buffer* buf = list_entry(ent, struct my_buffer, head);
        buf->mop->free(buf);
        ++n;
    }
    return n;
}

uint32_t inline calc_buffer_length(struct list_head* head)
{
    uint32_t n = 0;
    for (struct list_head* ent = head->next; ent != head; ent = ent->next) {
        struct my_buffer* buf = list_entry(ent, struct my_buffer, head);
        n += buf->length;
    }
    return n;
}

__attribute__((unused)) static void my_buffer_dump(struct my_buffer* mbuf)
{
    logmsg("mbuf(%p) dump %d bytes: ", mbuf, mbuf->length);
    for (int i = 0; i < mbuf->length; ++i) {
        const unsigned char* pch = (const unsigned char *) (mbuf->ptr[1] + i);
        logmsg("%02x ", pch[0]);
    }
    logmsg("\n");
}

#endif
