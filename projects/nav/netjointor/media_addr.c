
#include "media_addr.h"
#include "my_errno.h"
#include "mem.h"
#include <string.h>
#ifndef _MSC_VER
#include <arpa/inet.h>
#endif

#define keybytes 16

void netaddr_free(struct netaddr* addr)
{
    int n = __sync_sub_and_fetch(&addr->ref, 1);
    if (n == 0) {
        my_free(addr);
    }
}

void tcp_addr_free(struct tcp_addr* addr)
{
    for (uint16_t i = 0; i < addr->nr_addr; ++i) {
        if (addr->addr[i] != NULL) {
            netaddr_free(addr->addr[i]);
        }
    }
    my_free(addr->addr);
    my_free(addr);
}

static void tcp_addr_sort(struct tcp_addr* addr, uint16_t isp)
{
    uint16_t idx = 0;
    for (uint32_t i = 0; i < addr->nr_addr; ++i) {
        if (addr->addr[i]->isp == isp) {
            struct netaddr* tmp = addr->addr[idx];
            addr->addr[idx] = addr->addr[i];
            addr->addr[i] = tmp;
            idx += 1;
        }
    }
}

int tcpaddr_compile(char buf[1024], uint32_t sid, const char* ip, short port)
{
    char* info = (char *) buf;
    info += sizeof(uint16_t) * 2 + sizeof(uint64_t);

    *(uint32_t *) info = htonl(sid);
    info += sizeof(uint32_t);

    *(uint32_t *) info = htonl(3998);
    info += sizeof(uint32_t);

    memcpy(info, "2553205829555260", 16);
    info += 16;

    *(uint32_t *) info = htonl(1);
    info += sizeof(uint32_t);

    *(uint32_t *) info = 0;
    info += sizeof(uint32_t);

    uint16_t nb = (uint16_t) sprintf(&info[2], "%s:%d", ip, port);
    *(uint16_t *) info = htons(nb);
    info += sizeof(uint16_t) + nb;

    int n = (int) (info - buf);
    *(uint16_t *) buf = htons(n - 2);
    return n;
}

struct tcp_addr* tcp_addr_parsel(uint64_t uid, const uint8_t* info, uint32_t bytes, uint16_t isp)
{
    if (bytes < 36) {
        errno = EINVAL;
        return NULL;
    }
    bytes -= sizeof(uint16_t);

    struct tcp_addr* addr = (struct tcp_addr *) my_malloc(sizeof(struct tcp_addr) + keybytes);
    if (addr == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    addr->uid = htonll(uid);

    addr->key_bytes = keybytes;
    addr->keyp = (uint8_t *) (addr + 1);
    addr->addr = NULL;

    uint16_t int16 = ntohs(*(uint16_t *) info);
    if (int16 != bytes) {
        my_free(addr);
        errno = EINVAL;
        return NULL;
    }
    // jump version
    info += sizeof(uint16_t) * 2;

    addr->server = *(uint64_t *) info;
    info += sizeof(uint64_t);

    addr->sid = *(uint32_t *) info;
    info += sizeof(uint32_t);

    addr->token = *(uint32_t *) info;
    info += sizeof(uint32_t);

    memcpy(addr->keyp, info, addr->key_bytes);
    info += addr->key_bytes;

    addr->nr_addr = (uint16_t) ntohl(*(uint32_t *) info);
    info += sizeof(uint32_t);
    addr->addr = (struct netaddr **) my_malloc(sizeof(struct netaddr *) * addr->nr_addr);
    if (addr->addr == NULL) {
        errno = ENOMEM;
        my_free(addr);
        return NULL;
    }

    char buf[64];
    for (uint32_t i = 0; i < addr->nr_addr; ++i) {
        addr->addr[i] = (struct netaddr *) my_malloc(sizeof(struct netaddr));
        if (addr->addr[i] == NULL) {
            goto LABEL;
        }
        addr->addr[i]->ref = 1;
        addr->addr[i]->isp = ntohl(*(uint32_t *) info);
        info += sizeof(uint32_t);

        uint16_t bytes = ntohs(*(uint16_t *) info);
        my_assert(bytes < sizeof(buf));
        info += sizeof(uint16_t);
        memcpy(buf, info, bytes);
        info += bytes;
        buf[bytes] = 0;

        char* str = strchr(buf, ':');
        my_assert(str != NULL);
        *str = 0;

        strncpy(addr->addr[i]->ip, buf, 16);
        addr->addr[i]->port = atoi(str + 1);
    }

    tcp_addr_sort(addr, isp);
    return addr;
LABEL:
    tcp_addr_free(addr);
    return NULL;
}
