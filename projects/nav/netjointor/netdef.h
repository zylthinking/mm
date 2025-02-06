

#ifndef netdef_h
#define netdef_h

#define notify_new_stream 0
#define notify_connected 1
#define notify_disconnected 2
#define notify_login 3
#define notify_user_stream 4
#define notify_user_count 5
#define notify_user_stat 6
#define notify_statistic 7
#define notify_io_stat 8

typedef struct {
    uint64_t uid;
    uint32_t sid;
    uint32_t aud0_vid1;
    uintptr_t lost;
    uintptr_t recv;
} media_stat_t;

typedef struct {
    uint64_t nb0;
    uint64_t nb1;
} iostat_t;

typedef struct {
    uint64_t uid;
    uint32_t cid;
    uint32_t aud0_vid1;
    void* media;
} usr_stream_t;

typedef struct {
    uint64_t uid;
    uint64_t stat;
    uint64_t reason;
} usr_stat_t;

typedef struct {
    int (*fptr) (void* ctx, uint32_t notify, void* any);
    void* ctx;
} notify_fptr;

#define frames_per_packet 2

#endif
