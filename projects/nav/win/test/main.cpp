
#include "mydef.h"
#include "mmv.h"
#include "nav.h"
#include "mem.h"
#include "netdef.h"
#include "env.h"
#include "audiodef.h"
#include "now.h"
#include "media_addr.h"
#include <pthread.h>

#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
#pragma comment(lib, "../../win/bin/media.lib")
#define userid 3210
#else
#include <unistd.h>
#define userid 3210
#endif

static uint32_t interrupted = 0;
static void audio_env_changed(void* any)
{
    if (in_killed == any) {
        puts("input_manslaughter\n");
    } else if (out_killed == any) {
        puts("output_manslaughter\n");
    } else {
        audio_state* stat = (audio_state *) any;
        puts("--------------------------------------\n");
        printf("\tcategory: %d\n", stat->category);
        printf("\tspeaker: %d\n", stat->speaker);
        printf("\theadset: %d\n", stat->headset);
        printf("\tmicrophone: %d\n", stat->microphone);
        printf("\tinterrupted: %d\n", stat->interrupted);
        interrupted = stat->interrupted;
    }
}

static void capture_and_play(const char* filename)
{
    env_device_nofity(audio_env_changed);
LABEL:
#if 0
    void* ptr = mmvoice_capture_to_file(filename, type_child, ios);
    if (ptr == NULL) {
        if (EAGAIN == errno) {
            logmsg("mmvoice_capture_to_file need call agin\n");
        } else {
            logmsg("mmvoice_capture_to_file failed\n");
        }
        goto LABEL;
    }

    uint32_t current = now() * 1000;
    uint32_t target = current + 10 * 1000 * 1000;
    while (current < target) {
        logmsg("amplitude %d\n", amplitude(ptr));
        usleep(160 * 1000);
        current = now() * 1000;
    }
    mmvoice_capture_stop(ptr);
#endif

    playpos* pos = mmvoice_play_file(filename);
    if (pos != NULL) {
        while (pos->consumed != pos->total) {
            if (pos->err == 1) {
                logmsg("err occured when play file\n");
                break;
            }

            if (interrupted == 1) {
#ifdef __APPLE__
                lock_audio_dev();
#endif
            }
            logmsg("%d:%d\n", pos->consumed, pos->total);
            usleep(100 * 1000);
        }
        logmsg("%d:%d\n", pos->consumed, pos->total);

        usleep(200 * 1000);
        mmvoice_stop_play(pos);
    } else {
        logmsg("mmvoice_play_file failed\n");
    }

    logmsg("***memory %d\n\n", (int32_t) debug_mem_bytes());
    goto LABEL;
}

static int notify(void* tcp, uint32_t notify, void* any)
{
    if (notify == notify_connected) {
        logmsg("net connected\n");
    } else if (notify == notify_disconnected) {
        logmsg("net broken\n");
    } else if (notify == notify_login) {
        logmsg("session login ok\n");
    }
    return 0;
}

#define us_session (2000 * 1000)
#ifndef _MSC_VER
#define upload_us 0
#else
#define upload_us 0
#endif

static void net_audio(uint32_t us)
{
    char info[1024];
    int bytes = tcpaddr_compile(info, 100000008, "211.151.194.44", 80);

    nav_open_args args;
    args.uid = userid;
    args.info = (uint8_t *) &info[0];
    args.bytes = bytes;
    args.isp = 0;
    args.ms_wait = 1000;
    args.fptr.fptr = notify;
    args.fptr.ctx = NULL;
    args.dont_play = 0;
    args.dont_upload = 0;
    args.login_level = 0;

LABEL:
    void* something = nav_open(&args);
    if (something == NULL) {
        goto LABEL;
    }

    audio_param param;
    param.aec = ios;
    param.hook = type_none;

    do {
        int32_t n = nav_ioctl(something, audio_capture_start, (void *) &param);
        if (n == -1) {
            logmsg("audio_capture_start failed\n");
        } else if (upload_us) {
            usleep(upload_us);
            nav_ioctl(something, audio_capture_stop, NULL);
            logmsg("***memory %d\n\n", (int32_t) debug_mem_bytes());
        }

        n = 3;
        nav_ioctl(something, audio_enable_dtx, (void *) &n);
    } while (upload_us);

    usleep(us - upload_us);
    nav_close(something, 11);

    usleep(10000);
    logmsg("***memory %d\n\n", (int32_t) debug_mem_bytes());
    goto LABEL;
}

void* cap_and_play_thread(void* any)
{
    const char* pathname = (const char *) any;
    capture_and_play(pathname);
    return NULL;
}

void* net_audio_thread(void* any)
{
    usleep(15000);
    net_audio(1000 * 1000 * 3);
    return NULL;
}

int main(int argc, char *argv[])
{
#if 0
#ifdef _MSC_VER
    const char* buf = "e:\\a.silk";
    const char* buf2 = "e:\\b.silk";
#else
    char buf[640] = {0};
    char buf2[640] = {0};
    strcpy(buf, argv[0]);
    char* pch = strrchr(buf, '/');
    *pch = 0;
    pch = strrchr(buf, '/');
    *pch = 0;

    strcpy(buf2, buf);
    strcat(pch - 1, "/Documents/a.silk");
    strcat(buf2, "/Documents/b.silk");
#endif
    //pthread_t tid;
    //pthread_create(&tid, NULL, cap_and_play_thread, (void *) buf);
    //usleep(2 * 1000 * 1000);
    capture_and_play(buf);
#else
    pthread_t tid;
    pthread_create(&tid, NULL, net_audio_thread, NULL);
    net_audio(1000 * 1000 * 1);
#endif
    return 0;
}
