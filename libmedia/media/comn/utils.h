
#ifndef utils_h
#define utils_h

#include "mydef.h"
#include "my_buffer.h"

#ifndef _MSC_VER
#include <netinet/in.h>
#else

typedef struct tag_io_context {
    WSAOVERLAPPED overlap;
    WSABUF wsa_buffer;
    struct my_buffer* base;
    struct sockaddr addr;
    int addr_size;
    int io_type;
    int flag;
    struct list_head entry;
} io_context;
#define hand_shake  ((struct my_buffer *) 1)

#endif

capi int my_socket(int af, int type, int protocol);
capi void my_close(int fd);
capi int make_none_block(int fd);
capi int make_no_sigpipe(int fd);
capi int do_connect(int fd, const char* ip, short port);
capi int check_connection(int fd);
capi int do_tcp_read(int fd, struct my_buffer* mbuf);
capi int do_tcp_write(int fd, struct my_buffer* mbuf);
capi int do_udp_read(int fd, struct my_buffer* mbuf, struct sockaddr_in* inptr);
capi int do_udp_write(int fd, struct my_buffer* mbuf, struct sockaddr_in* in);

#endif
