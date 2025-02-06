
#include <stdio.h>
#include "lock.h"
#include <time.h>
#include <android/log.h>

const char* pcm_path = NULL;
static FILE* logfile_get()
{
    static char name[1024];
    const char* path = "/sdcard";
#if logpcm_enable
    pcm_path = path;
#endif

    intptr_t len = strlen(path);
    strcpy(name, path);

    strcat(name, "/zyl");
    FILE* file = fopen(name, "w");
    return file;
}

void logbuffer(const char * __restrict fmt, ...)
{
    static lock_t lck = lock_initial;
    static FILE* logfile = NULL;
    int err = errno;

    if (logfile == NULL) {
        intptr_t init_by_me = 0;
        lock(&lck);
        if (logfile == NULL) {
            logfile = logfile_get();
            init_by_me = 1;
        }
        unlock(&lck);

        if (init_by_me != 0) {
            time_t clk;
            time(&clk);
            struct tm* tm = localtime(&clk);
            logmsg("time base: %02d:%02d:%02d %p\n", tm->tm_hour, tm->tm_min, tm->tm_sec, &logfile);
        }
    }

    va_list ap;
    va_start(ap, fmt);

    if (logfile != NULL) {
        lock(&lck);
        fprintf(logfile, "%u: ", now());
        vfprintf(logfile, fmt, ap);
        fflush(logfile);
        unlock(&lck);
        va_end(ap);
        va_start(ap, fmt);
    }

    __android_log_vprint(ANDROID_LOG_ERROR, "zylthinking", fmt, ap);
    va_end(ap);
    errno = err;
}
