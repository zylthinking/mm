
#ifndef luaconf_h
#define luaconf_h

#include "mem.h"
#include <time.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <sys/inotify.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

struct watcher {
    int inode_fd;
    pthread_t tid;
    int changed;
    uint32_t lastcheck;
    char file[640];
};

static void* watch_main(void* ptr)
{
    struct watcher* watch = (struct watcher *) ptr;
    char buf[sizeof(struct inotify_event) + 640];

    while (true) {
        int n = read(watch->inode_fd, &buf, sizeof(buf));
        if (n == -1) {
            continue;
        }

        if (n == 0) {
            break;
        }
        watch->changed = 1;
    }

    close(watch->inode_fd);
    watch->inode_fd = -1;
    return NULL;
}

static struct watcher* watcher_create(const char* path)
{
    static char directory[640];
    static struct watcher watch = {-1, 0, 1, 0, {0}};
    if (path != &watch.file[0]) {
        strcpy(watch.file, path);
        strcpy(directory, path);
        char* pch = strrchr(directory, '/');
        *pch = 0;
    }

    watch.inode_fd = inotify_init();
    if (watch.inode_fd == -1) {
        return NULL;
    }

    int wd = inotify_add_watch(watch.inode_fd, directory, IN_CREATE | IN_MODIFY);
    if (wd == -1) {
        close(watch.inode_fd);
        watch.inode_fd = -1;
        return NULL;
    }

    int n = pthread_create(&watch.tid, NULL, watch_main, (void *) &watch);
    if (n != 0) {
        close(watch.inode_fd);
        watch.inode_fd = -1;
        return NULL;
    }

    watch.lastcheck = time(NULL);
    watch.changed = 1;
    return &watch;
}

static void watch_touch(struct watcher* watch)
{
    uint32_t current = time(NULL);
    if (watch->lastcheck == 0) {
LABEL:
        watcher_create(watch->file);
    } else if (current - watch->lastcheck > 30) {
        int n = pthread_kill(watch->tid, 0);
        if (n == ESRCH) {
            pthread_join(watch->tid, NULL);
            watch->lastcheck = 0;
            goto LABEL;
        }
        watch->lastcheck = current;
    }
}

static char* save_string(const char* pch)
{
    char* value = NULL;
    if (pch != NULL) {
        size_t bytes = strlen(pch) + 1;
        value = (char *) my_malloc(bytes);
        if (value != NULL) {
            strcpy(value, pch);
        }
    }
    return value;
}

static char* conf_string(lua_State* L, const char* key)
{
    lua_pushstring(L, key);
    lua_gettable(L, -2);
    const char* pch = lua_tostring(L, -1);
    lua_pop(L, 1);
    return save_string(pch);
}

#define conf_number(L, key, val) \
({ \
    lua_pushstring(L, key); \
    lua_gettable(L, -2); \
    int succeed; \
    lua_Integer num = lua_tointegerx(L, -1, &succeed); \
    if (succeed) { \
        val = (typeof(val)) num; \
        succeed = 0; \
    } else { \
        succeed = -1; \
    } \
    lua_pop(L, 1); \
    succeed; \
})

static void free_string(char** pch, int n)
{
    for (int i = 0; i < n; ++i) {
        my_free(pch[i]);
    }
    my_free(pch);
}

static int conf_string_array(lua_State* L, const char* key, char*** array)
{
    int n = 0;
    char** pch = NULL;
    lua_pushstring(L, key);
    lua_gettable(L, -2);

    size_t size = lua_rawlen(L, -1);
    if (size > 0) {
        pch = (char **) my_malloc(sizeof(char *) * size);
        if (pch == NULL) {
            return -1;
        }
    }

    for (int i = 1; i <= size; ++i) {
        lua_rawgeti (L, -1, i);
        const char* str = lua_tostring(L, -1);
        pch[i - 1] = save_string(str);
        lua_pop(L, 1);
        if (pch[i - 1] == NULL) {
            n = i - 1;
            break;
        }
    }
    lua_pop(L, 1);

    if (n == 0) {
        array[0] = pch;
        return (int) size;
    }

    free_string(pch, n);
    return -1;
}

extern int32_t do_load_config(lua_State* L, const char* file, void*);

static int32_t load_config(const char* id, const char* filename, void* any)
{
    static struct watcher* watch = NULL;
    if (watch == NULL) {
        watch = watcher_create(filename);
        if (watch == NULL) {
            return -1;
        }
    } else {
        watch_touch(watch);
    }

    if (0 == __sync_lock_test_and_set(&watch->changed, 0)) {
        return 0;
    }

    lua_State* L = luaL_newstate();
    if (L == NULL) {
        logmsg("failed to create config file parser\n");
        return -1;
    }
    luaopen_base(L);

    if (LUA_ERRFILE == luaL_loadfile(L, watch->file)) {
        logmsg("no config file %s\n", watch->file);
        lua_close(L);
        return -1;
    }

    int32_t n = lua_pcall(L, 0, 0, 0);
    if (LUA_OK != n) {
        const char* pch = lua_tostring(L, -1);;
        logmsg("parse %s failed with err %d, %s\n", watch->file, n, pch);
        lua_close(L);
        return -1;
    }
    lua_getglobal(L, "config_table");
    lua_pushstring(L, id);

    n = lua_pcall(L, 1, 1, 0);
    if (LUA_OK != n) {
        const char* pch = lua_tostring(L, -1);;
        logmsg("parse %s failed with err %d, %s\n", watch->file, n, pch);
        lua_close(L);
        return -1;
    }

    n = do_load_config(L, filename, any);
    lua_close(L);
    return n;
}

#endif
