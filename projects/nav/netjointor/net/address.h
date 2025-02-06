
#ifndef address_h
#define address_h

#include "mydef.h"
#include "lock.h"
#include "list_head.h"

typedef struct {
    lock_t lck;
    struct list_head head;
    struct sockaddr_in* used;
} book_t;

#define init_book(book) \
do { \
    (book)->lck = lock_val; \
    INIT_LIST_HEAD(&((book)->head)); \
    (book)->used = NULL; \
} while (0)

capi int32_t address_new(book_t* book, uint32_t ip, uint16_t* port, uint32_t nr, uint32_t stable);
capi int32_t address_renew(book_t* book, uint32_t ip, uint32_t stable);
capi struct sockaddr_in* sockaddr_get(book_t* book, struct sockaddr_in* old);
capi void sockaddr_put(book_t* book, struct sockaddr_in* in);
capi struct sockaddr_in* sockaddr_delete(book_t* book, uint32_t ip);

#endif
