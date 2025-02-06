
#include "address.h"
#include "mem.h"
#include "my_errno.h"
#include <string.h>

#ifndef _MSC_VER
#include <netinet/in.h>
#endif

typedef struct {
    struct list_head entry;
    struct sockaddr_in addr;
    uint32_t stable;
    uint32_t counter[2];
    uint16_t* ports;
} address_t;

int32_t address_new(book_t* book, uint32_t ip, uint16_t* port, uint32_t nr, uint32_t stable)
{
    my_assert(port != NULL && nr != 0);
    address_t* addr = (address_t *) my_malloc(sizeof(address_t) + sizeof(uint16_t) * nr);
    if (addr == NULL) {
        errno = ENOMEM;
        return -1;
    }

#if 0
    for (intptr_t i = 0; i < nr; ++i) {
        logmsg("ip 0x%x, port: 0x%x add to book\n", ip, port[i]);
    }
#endif

    addr->stable = stable;
    addr->counter[1] = nr;
    addr->counter[0] = addr->counter[1] - 1;
    addr->ports = (uint16_t *) (addr + 1);

    memcpy(addr->ports, port, sizeof(uint16_t) * nr);
    addr->addr.sin_family = AF_INET;
    addr->addr.sin_port = port[0];
    addr->addr.sin_addr.s_addr = ip;
    memset(&addr->addr.sin_zero, 0, sizeof(addr->addr.sin_zero));

    lock(&book->lck);
    // I assume only one address_t have stable > 0
    struct list_head* ent = &book->head;
    if (stable == 0 && !list_empty(ent)) {
        address_t* addr = list_entry(ent->next, address_t, entry);
        if (addr->stable > 0) {
            ent = ent->next;
        }
    }
    list_add(&addr->entry, ent);
    unlock(&book->lck);
    return 0;
}

static struct sockaddr_in* do_sockaddr_get(address_t* addr)
{
    addr->counter[0]++;
    if (addr->counter[0] == addr->counter[1]) {
        addr->counter[0] = 0;
    }

    uint32_t idx = addr->counter[0];
    addr->addr.sin_port = addr->ports[idx];
    return &addr->addr;
}

struct sockaddr_in* sockaddr_get(book_t* book, struct sockaddr_in* old)
{
    address_t* addr = NULL;
    struct list_head* ent = NULL;
    if (old != NULL) {
        my_assert(!list_empty(&book->head));
        ent = &(container_of(old, address_t, addr)->entry);
    }

    lock(&book->lck);
    my_assert(book->used == old);

    if (ent != book->head.next) {
        ent = book->head.next;
    } else {
        // old is prefered, enum to next to avoid use it
        // because old is unreachable (at least currently)
        ent = ent->next;
        // old is the only one, have to use it again.
        if (ent == &book->head) {
            ent = book->head.next;
        }
    }

    if (ent != &book->head) {
        addr = list_entry(ent, address_t, entry);
        if (addr->stable == 0 || addr->stable == 1) {
            list_del(ent);
            list_add_tail(ent, &book->head);
        } else {
            --addr->stable;
        }
    }

    book->used = NULL;
    if (addr == NULL) {
        unlock(&book->lck);
        return NULL;
    }

    book->used = &addr->addr;
    unlock(&book->lck);
    return do_sockaddr_get(addr);
}

struct sockaddr_in* sockaddr_delete(book_t* book, uint32_t ip)
{
    struct sockaddr_in* in = NULL;
    struct list_head *next, *ent;

    lock(&book->lck);
    for (ent = book->head.next; ent != &book->head; ent = next) {
        address_t* addr = list_entry(ent, address_t, entry);
        next = ent->next;

        if (ip == 0 || addr->addr.sin_addr.s_addr == ip) {
            list_del_init(ent);
            if (book->used == &addr->addr) {
                in = book->used;
                book->used = NULL;
            } else {
                my_free(addr);
            }

            if (ip != 0) {
                break;
            }
        }
    }
    unlock(&book->lck);

    return in;
}

void sockaddr_put(book_t* book, struct sockaddr_in* in)
{
    address_t* addr = container_of(in, address_t, addr);
    lock(&book->lck);
    if (book->used != in) {
        my_assert(list_empty(&addr->entry));
        my_free(addr);
    } else {
        book->used = NULL;
        list_del(&addr->entry);
        list_add(&addr->entry, &book->head);
    }
    unlock(&book->lck);
}

int32_t address_renew(book_t* book, uint32_t ip, uint32_t stable)
{
    my_assert(stable != 0);
    int32_t n = -1;
    struct list_head *next, *ent;

    lock(&book->lck);
    for (ent = book->head.next; ent != &book->head; ent = next) {
        next = ent->next;
        address_t* addr = list_entry(ent, address_t, entry);
        if (addr->addr.sin_addr.s_addr == ip) {
            if (addr->stable > 0) {
                addr->stable = stable;
            }
            logmsg("recv packet from 0x%x, renew it\n", ip);
            list_del(&addr->entry);
            list_add(&addr->entry, &book->head);
            n = 0;
            break;
        }
    }
    unlock(&book->lck);

    return n;
}
