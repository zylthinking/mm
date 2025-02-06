
#ifndef media_addr_h
#define media_addr_h

#include "mydef.h"

struct netaddr {
    int ref;
    uint32_t isp;
    char ip[16];
    uint16_t port;
};

struct tcp_addr {
    uint64_t server;
    uint64_t uid;
    uint32_t sid;
    uint32_t token;

    uint16_t key_bytes;
    uint16_t nr_addr;
    uint8_t* keyp;
    struct netaddr** addr;
};

// all machine supported is little endian
// the guy introduced big endian in the protocol actually is brain damaged
#ifndef ntohll
static __attribute__((unused)) uint64_t ntohll(uint64_t ll)
{
    char ch, *pch = (char *) &ll;
    ch = pch[0]; pch[0] = pch[7]; pch[7] = ch;
    ch = pch[1]; pch[1] = pch[6]; pch[6] = ch;
    ch = pch[2]; pch[2] = pch[5]; pch[5] = ch;
    ch = pch[3]; pch[3] = pch[4]; pch[4] = ch;
    return ll;
}
#endif

#ifndef htonll
#define htonll ntohll
#endif

capi int tcpaddr_compile(char buf[1024], uint32_t sid, const char* ip, short port);
capi struct tcp_addr* tcp_addr_parsel(uint64_t uid, const uint8_t* info, uint32_t bytes, uint16_t isp);
capi void netaddr_free(struct netaddr* addr);
capi void tcp_addr_free(struct tcp_addr* addr);

#endif
