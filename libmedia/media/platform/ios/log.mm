
#include <stdio.h>
#include "lock.h"
#include <time.h>
#import <Foundation/Foundation.h>

const char* pcm_path = NULL;
static FILE* logfile_get()
{
    static char name[1024];

    NSString* folder = nil;
    if (folder == nil) {
        folder = NSTemporaryDirectory();
    }
    const char* path = [folder UTF8String];

    intptr_t len = strlen(path);
    strcpy(name, path);

    if (name[len - 1] == '/') {
        name[len - 1] = 0;
    }

#if logpcm_enable
    static char pcm[1024];
    strcpy(pcm, name);
    pcm_path = pcm;
#endif

    strcat(name, "/zyl");
    FILE* file = fopen(name, "w");
    return file;
}

extern "C" void logbuffer(const char * __restrict fmt, ...)
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
            logmsg("time base: %02d:%02d:%02d\n", tm->tm_hour, tm->tm_min, tm->tm_sec);
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

    vprintf(fmt, ap);
    va_end(ap);
    errno = err;
}
