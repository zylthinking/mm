
#ifndef semphore_h
#define semphore_h

#include "mydef.h"
#ifdef _MSC_VER
#define sem_t uintptr_t
#else
#include <semaphore.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

typedef struct {
    void* ptr;
    const char* pch;

    char name[256];
    sem_t obj;
} semphore_t;

#ifdef _MSC_VER
static intptr_t do_semaphore_init(semphore_t* sem, uintptr_t nr)
{
    errno = EFAILED;
    HANDLE handle = CreateSemaphore(NULL, nr, 0x7fffffff, NULL);
    if (handle == NULL) {
        return -1;
    }
    sem->ptr = (void *) handle;
    sem->pch = sem->name;
    sem->name[0] = 0;
    return 0;
}
#define semaphore_init(sem, nr) do_semaphore_init(sem, nr)

static int semaphore_post(semphore_t* sem)
{
    my_assert(sem->ptr != NULL);
    errno = EFAILED;
    BOOL b = ReleaseSemaphore((HANDLE) sem->ptr, 1, NULL);
    if (!b) {
        return -1;
    }
    return 0;
}

static int semaphore_wait(semphore_t* sem)
{
    my_assert(sem->ptr != NULL);
    WaitForSingleObject((HANDLE) sem->handle, (DWORD) -1);
    return 0;
}

static int semaphore_destroy(semphore_t* sem)
{
    CloseHandle((HANDLE) sem->ptr);
    sem->ptr = NULL;
    sem->pch = NULL;
    return 0;
}

#else
static intptr_t do_semaphore_init(semphore_t* sem, uintptr_t nr, const char* func, intptr_t line)
{
    sem_t* ptr = NULL;
#if defined(__APPLE__)
    if (sem->ptr != NULL) {
        sem_unlink(sem->pch);
        sem->ptr = NULL;
        sem->pch = NULL;
    }

    sem->pch = sem->name;
    sprintf(sem->name, "/%s/%d", func, (int) line);
    sem_unlink(sem->pch);
    ptr = sem_open(sem->pch, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, (unsigned int) nr);
#else
    ptr = &sem->obj;
    int n = sem_init(&sem->obj, 0, (unsigned int) nr);
    if (n == -1) {
        ptr = SEM_FAILED;
    }
#endif

    if (SEM_FAILED == ptr) {
        return -1;
    }
    sem->ptr = ptr;
    return 0;
}

#define semaphore_init(sem, nr) do_semaphore_init(sem, nr, __FUNCTION__, __LINE__)
#define semaphore_post(sem) sem_post((sem_t *) (sem)->ptr)
#define semaphore_wait(sem) sem_wait((sem_t *) (sem)->ptr)

static int semaphore_destroy(semphore_t* sem)
{
    my_assert(sem->ptr != NULL);
#if defined(__APPLE__)
    sem_close((sem_t *) sem->ptr);
    if (0 == sem_unlink(sem->pch)) {
        sem->ptr = NULL;
        sem->pch = NULL;
    }
#else
    sem_destroy((sem_t *) sem->ptr);
    sem->ptr = NULL;
    sem->pch = NULL;
#endif
    return 0;
}
#endif

#endif
