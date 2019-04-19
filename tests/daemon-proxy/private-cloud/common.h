#include <stdio.h>  //printf
#include <string.h> //strlen
#include <sys/socket.h> //socket
#include <arpa/inet.h>  //inet_addr
#include <unistd.h>
#include <fstream>
#include <sys/wait.h>

#define MSG_SZ 128

#define SA struct sockaddr

struct s_handle {
    xclDeviceHandle uDevHandle;
};

struct drm_xocl_sw_mailbox {
    size_t sz;
    uint64_t flags;
    bool is_tx;
    uint64_t id;
    uint32_t *data;
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

int read_from_func( int handle, struct drm_xocl_sw_mailbox *args, size_t &alloc_sz )
{
    int ret = 0;
    args->is_tx = true;
    args->sz = alloc_sz;
    std::cout << "debug #before read\n";
    ret = read( handle, (void *)args, (sizeof(struct drm_xocl_sw_mailbox) + args->sz) );
    std::cout << "debug #after read\n";
    if( ret <= 0 ) {
        // sw channel xfer error
        if( errno != EMSGSIZE ) {
            std::cout << "              read: transfer failed for other reason\n";
            return errno;
        }
        // buffer was of insufficient size, resizing
        if( resize_buffer( args->data, args->sz ) != 0 ) {
            std::cout << "              read: resize_buffer() failed...exiting\n";
            return errno;
        }
        alloc_sz = args->sz; // store the newly alloc'd size
        ret = read( handle, (void *)args, (sizeof(struct drm_xocl_sw_mailbox) + args->sz) );
        if( ret < 0 ) {
            std::cout << "read: second transfer failed, exiting.\n";
            return errno;
        }
    }
    std::cout << "                [read from func]\n";
    return 0;
}

int write_to_func( int handle, struct drm_xocl_sw_mailbox *args )
{
    int ret = 0;
    args->is_tx = false;
    ret = write( handle, (void *)args, (sizeof(struct drm_xocl_sw_mailbox) + args->sz) );
    if( ret <= 0 ) {
        std::cout << "write: transfer error: " << strerror(errno) << std::endl;
        return errno;
    }
    return 0;
}

int write_args( int fd, struct drm_xocl_sw_mailbox *args )
{
    if( write( fd, (const void*)(args), sizeof(struct drm_xocl_sw_mailbox) ) == -1 )
        exit(1);
    return 0;
}

int write_data( int fd, uint32_t *buf, int buflen )
{
    void *vbuf = reinterpret_cast<void*>(buf);
    unsigned char *pbuf = (unsigned char *)vbuf;

    while( buflen > 0 ) {
        int num = write( fd, pbuf, buflen );
        pbuf += num;
        buflen -= num;
    }

    return 0;
}

int read_args( int fd, char* msg_buf, struct drm_xocl_sw_mailbox *args )
{
    int num = read( fd, msg_buf, sizeof(struct drm_xocl_sw_mailbox) );
    if( num < 0 )
        return num;

    memcpy( (void *)args, (void *)msg_buf, sizeof(struct drm_xocl_sw_mailbox) );
    std::cout  << "args->sz: " << args->sz << std::endl;
    return num;
}

int read_data( int fd, uint32_t *pdata, int buflen )
{
    int num = 0;
    unsigned char *pbuf = reinterpret_cast<unsigned char*>(pdata);
    while( buflen > 0 ) {
        num = read( fd, pbuf, buflen );

        if( num == 0 )
            break;

        if( num < 0 ) {
            std::cout << "Error, failed to read: " << num << ", errno: " << errno << " errstr: " << strerror(errno) << std::endl;
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

static void local_fini(int handle)
{
    close(handle);
}

