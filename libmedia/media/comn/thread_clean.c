

#include "thread_clean.h"
#include "mem.h"
#include <pthread.h>

typedef struct {
    free_t fptr;
    void* uptr;
} clean_entry_t;

static pthread_key_t process_key[process_key_nr];

static void key_destroy(void* value)
{
    clean_entry_t* entry = (clean_entry_t *) value;
    entry->fptr(entry->uptr);
    my_free(entry);
}

static __attribute__((constructor)) void key_init()
{
    for (int i = 0; i < elements(process_key); ++i) {
        int n = pthread_key_create(&process_key[i], key_destroy);
        if (n != 0) {
            printf("failed to create key\n");
            exit(-1);
        }
    }
}

void* thread_clean_push(intptr_t idx, free_t fptr, size_t bytes)
{
    pthread_key_t key = process_key[idx];
    clean_entry_t* entry = (clean_entry_t *) pthread_getspecific(key);
    if (entry != NULL) {
        return entry->uptr;
    }

    entry = (clean_entry_t *) my_malloc(sizeof(clean_entry_t) + bytes);
    if (entry == NULL) {
        return NULL;
    }

    entry->fptr = fptr;
    entry->uptr =  (void *) (entry + 1);
    if (0 != pthread_setspecific(key, entry)) {
        my_free(entry);
        return NULL;
    }

    if (bytes > 0) {
        memset(entry->uptr, 0, bytes);
    }
    return entry->uptr;
}

