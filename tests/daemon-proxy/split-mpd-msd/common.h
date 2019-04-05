#include <stdio.h>  //printf
#include <string.h> //strlen
#include <sys/socket.h> //socket
#include <arpa/inet.h>  //inet_addr
#include <unistd.h>
#include <fstream>
////* from brian */
///#include <netdb.h>
///#include <stdio.h>
///#include <stdlib.h>
///#include <string.h>
///#include <sys/socket.h>
///#include <netinet/in.h>
///#include <arpa/inet.h>
///#include <sys/types.h>
///#include <sys/stat.h>
///#include <fcntl.h>
///#include <net/if.h>
///#include <linux/if_tun.h>
///#include <sys/ioctl.h>
#include <sys/wait.h>
///#include <unistd.h>

#define MSG_SZ 128

#define SA struct sockaddr

struct s_handle {
    xclDeviceHandle uDevHandle;
};

struct drm_xocl_sw_mailbox {
    uint64_t flags;
    uint32_t *data;
    bool is_tx;
    size_t sz;
    uint64_t id;
};

int resize_buffer( uint32_t *&buf, const size_t new_sz )
{
    if( buf != NULL ) {
        free(buf);
        buf = NULL;
    }
    buf = (uint32_t *)malloc( new_sz );
    if( buf == NULL ) {
        return -1;
    }

    return 0;
}

int send_args( int sock, struct drm_xocl_sw_mailbox *args )
{
    if( send( sock, (const void*)(args), sizeof(struct drm_xocl_sw_mailbox), 0 ) == -1 )
        exit(1);
    return 0;
}

int send_data( int sock, uint32_t *buf, int buflen )
{
    void *vbuf = reinterpret_cast<void*>(buf);
    unsigned char *pbuf = (unsigned char *)vbuf;

    while( buflen > 0 ) {
        int num = send( sock, pbuf, buflen, 0 );
        pbuf += num;
        buflen -= num;
    }

    return 0;
}

int recv_args( int sock, char* msg_buf, struct drm_xocl_sw_mailbox *args )
{
    int num = recv( sock, msg_buf, sizeof(struct drm_xocl_sw_mailbox), 0 );
    if( num < 0 )
        return num;

    memcpy( (void *)args, (void *)msg_buf, sizeof(struct drm_xocl_sw_mailbox) );
    std::cout  << "args->sz: " << args->sz << std::endl;
    return num;
}

int recv_data( int sock, uint32_t *pdata, int buflen )
{
    int num = 0;
    unsigned char *pbuf = reinterpret_cast<unsigned char*>(pdata);
    while( buflen > 0 ) {
        num = recv( sock, pbuf, buflen, 0 );

        if( num == 0 )
            break;

        if( num < 0 ) {
            std::cout << "Error, failed to recv: " << num << ", errno: " << errno << " errstr: " << strerror(errno) << std::endl;
            return num;
        }
        pbuf += num;
        buflen -= num;
        std::cout << "buflen=" << buflen << ", num=" << num << std::endl;
    }
    std::cout << "transfer complete: buflen=" << buflen << std::endl;
    return 0;
}

/*
 * Stub code that finishs the communication channel.
 * Cloud vendor should implement this function.
 */
static void comm_fini(int handle)
{
    close(handle);
}
