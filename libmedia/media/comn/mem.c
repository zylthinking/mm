
#include "mem.h"
#include "list_head.h"
#include "lock.h"
#include <string.h>

#if debug_mem
#define check_bytes 0

struct memory_link {
    struct list_head entry;
    uintptr_t bytes;
    uintptr_t line;
    const char* name;
};

static LIST_HEAD(mem_head);
static size_t total = 0;
static lock_t llk = lock_initial;

void* debug_malloc(size_t n, const char* func, int line)
{
    void* ptr = NULL;
    size_t len = sizeof(struct memory_link) + n;
    if (len > sizeof(struct memory_link)) {
        if (0 != posix_memalign(&ptr, sizeof(void *), len + check_bytes)) {
            ptr = NULL;
        }

        if (ptr != NULL) {
            struct memory_link* link = (struct memory_link *) ptr;
            link->bytes = len;
            link->line = line;
            link->name = func;
            ptr = (void *) (link + 1);

            if (check_bytes) {
                char* ptr2 = ((char *) ptr) + n;
                memset(ptr2, 0, check_bytes);
                strncpy(ptr2, func, check_bytes - 1);
            }

            lock(&llk);
            total += link->bytes - sizeof(struct memory_link);
            list_add(&link->entry, &mem_head);
            unlock(&llk);
        }
    }
    return ptr;
}

static void validate(struct memory_link* link)
{
    // to avoid warnings when check_bytes == 0
    static char example[check_bytes + 1] = {0};

    if (0x13572468 == link->line) {
        logmsg("bugs: the memory has been freed\n");
        my_assert(0);
        return;
    }

    if (check_bytes == 0) {
        return;
    }

    char* ptr = ((char *) link) + link->bytes;
    if (0 != strcmp(ptr, link->name)) {
        logmsg("bugs: %s[%s] the memory has been corrupted\n", ptr, link->name);
        my_assert(0);
    } else {
        size_t bytes = strlen(link->name);
        if (0 != memcmp(ptr + bytes, example, check_bytes - bytes)) {
            logmsg("bugs: %s[%s] the memory has been corrupted\n", ptr, link->name);
            my_assert(0);
        }
    }
}

void check_memory(void* ptr)
{
    struct memory_link* link = ((struct memory_link *) ptr) - 1;
    validate(link);
}

void debug_free(void* ptr)
{
    if (ptr != NULL) {
        struct memory_link* link = ((struct memory_link *) ptr) - 1;
        validate(link);

        lock(&llk);
        list_del(&link->entry);
        total -= link->bytes - sizeof(struct memory_link);
        link->line = 0x13572468;
        unlock(&llk);

        posix_free((void *) link);
    }
}

void my_tell(void* ptr)
{
    if (ptr != NULL) {
        struct memory_link* link = ((struct memory_link *) ptr) - 1;
        logmsg("func: %s, line: %d, bytes = %d\n",
               link->name, link->line, (int) (link->bytes - sizeof(struct memory_link)));
    }
}

size_t debug_mem_bytes()
{
    return total;
}

void debug_mem_stats(printf_t fptr)
{
    lock(&llk);
    struct list_head* ent;
    for (ent = mem_head.next; ent != &mem_head; ent = ent->next) {
        struct memory_link* link = list_entry(ent, struct memory_link, entry);
        validate(link);

        if (fptr == NULL) {
            continue;
        }
        uint32_t bytes = (uint32_t) (link->bytes - sizeof(struct memory_link));
        fptr("%d@%s, %p, bytes: %d\n", link->line, link->name, link + 1, bytes);
    }
    unlock(&llk);
}

#endif
