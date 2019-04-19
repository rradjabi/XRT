/*
 * ryan.radjabi@xilinx.com
 *
 * Simple passthrough daemon reference.
 */
#include <dirent.h>
#include <iterator>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <iostream>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <vector>
#include <stdbool.h>
#include <stdint.h>
#include <stdexcept>
#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <libdrm/drm.h>
#include <pthread.h>
#include <time.h>
#include <getopt.h>

#include "xclhal2.h"

#define INIT_BUF_SZ 64

pthread_t mpd_id;
pthread_t msd_id;

struct s_handles {
        int userfd;
        int mgmtfd;
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
        std::cout << "alloc failed \n";
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



/*
 * MPD - Management Proxy Daemon
 * This thread passes messages from userpf->mgmtpf.
 */
void *mpd(void *handle_ptr)
{
    int xferCount = 0;
    int ret;
    struct s_handles *h = (struct s_handles *)handle_ptr;
    size_t prev_sz = INIT_BUF_SZ;
    struct drm_xocl_sw_mailbox args = { prev_sz, 0, true, 0, 0 };
    args.data = (uint32_t *)malloc(prev_sz);

    std::cout << "[XOCL->XCLMGMT Intercept ON (HAL)]\n";
    for( ;; ) {
/* read from userpf */
        //~ args.is_tx = true;
        //~ args.sz = prev_sz;
        //~ ret = read( h->userfd, &args, (sizeof(struct drm_xocl_sw_mailbox) + args.sz) );
        //~ if( ret <= 0 ) {
            //~ // sw channel xfer error
            //~ if( errno != EMSGSIZE ) {
                //~ std::cout << "MPD: transfer failed for other reason\n";
                //~ exit(1);
            //~ }
            //~ // buffer was of insufficient size, resizing
            //~ if( resize_buffer( args.data, args.sz ) != 0 ) {
                //~ exit(1);
            //~ }
            //~ prev_sz = args.sz; // store the newly alloc'd size
            //~ ret = read( h->userfd, &args, (sizeof(struct drm_xocl_sw_mailbox) + args.sz) );
            //~ if( ret < 0 ) {
                //~ std::cout << "MPD: second transfer failed, exiting.\n";
                //~ exit(1);
            //~ }
        //~ }
        //~ std::cout << "[MPD-TX]\n";
        std::cout << "debug #before read_from_func\n";
        if( read_from_func(h->userfd, &args, prev_sz) )
            exit(errno);
        std::cout << "debug #after read_from_func\n";
        std::cout << "[MPD-TX]\n";
/* read from userpf complete */


/* write to mgmtpf */
        //~ args.is_tx = false;
        //~ ret = write( h->mgmtfd, &args, (sizeof(struct drm_xocl_sw_mailbox) + args.sz) );
        //~ if( ret <= 0 ) {
            //~ std::cout << "MSD: transfer error: " << strerror(errno) << std::endl;
            //~ exit(1);
        //~ }
        if( write_to_func(h->mgmtfd, &args) )
            exit(errno);
/* write to mgmtpf complete */

        std::cout << "[MSD-RX]: " << xferCount << std::endl;
        xferCount++;
    }
    std::cout << "Exit thread XOCL->XCLMGMT\n";
}

void *msd(void *handle_ptr)
{
    int xferCount = 0;
    int ret;
    struct s_handles *h = (struct s_handles *)handle_ptr;
    size_t prev_sz = INIT_BUF_SZ;
    struct drm_xocl_sw_mailbox args = { prev_sz, 0, true, 0, 0 };
    args.data = (uint32_t *)malloc(prev_sz);

    std::cout << "               [XCLMGMT->XOCL Intercept ON (HAL)]\n";
    for( ;; ) {
/* read from mgmtpf */
        //~ args.is_tx = true;
        //~ args.sz = prev_sz;
        //~ ret = read( h->mgmtfd, &args, (sizeof(struct drm_xocl_sw_mailbox) + args.sz) );
        //~ if( ret <= 0 ) {
            //~ // sw channel xfer error
            //~ if( errno != EMSGSIZE ) {
                //~ std::cout << "              MSD: transfer failed for other reason\n";
                //~ exit(1);
            //~ }
            //~ // buffer was of insufficient size, resizing
            //~ if( resize_buffer( args.data, args.sz ) != 0 ) {
                //~ std::cout << "              MSD: resize_buffer() failed...exiting\n";
                //~ exit(1);
            //~ }
            //~ prev_sz = args.sz; // store the newly alloc'd size
            //~ ret = read( h->mgmtfd, &args, (sizeof(struct drm_xocl_sw_mailbox) + args.sz) );
            //~ if( ret < 0 ) {
                //~ std::cout << "MPD: second transfer failed, exiting.\n";
                //~ exit(1);
            //~ }
        //~ }
        //~ std::cout << "                [MSD-TX]\n";
        if( read_from_func(h->mgmtfd, &args, prev_sz) )
            exit(errno);
        std::cout << "                [MSD-TX]\n";
/* read from mgmtpf complete */

/* write to userpf */
        //~ args.is_tx = false;
        //~ ret = write( h->userfd, &args, (sizeof(struct drm_xocl_sw_mailbox) + args.sz) );
        //~ if( ret <= 0 ) {
            //~ std::cout << "                    MPD: transfer error: " << strerror(errno) << std::endl;
            //~ exit(1);
        //~ }
        if( write_to_func(h->userfd, &args) )
            exit(errno);
/* write to userpf complete */

        std::cout << "                [MPD-RX] " << xferCount << std::endl;
        xferCount++;
    }
    std::cout << "Exit thread XCLMGMT->XOCL\n";
}

int main(void)
{
    struct s_handles handles;

    const int numDevs = xclProbe();
    for (int i = 0; i < numDevs; i++) {
        handles.userfd = xclMailbox( i );
        handles.mgmtfd = xclMailboxMgmt( i );

        int ret = fork();
        if (ret < 0) {
            std::cout << "Failed to create child process: " << errno << std::endl;
            exit( errno );
        }
        if( ret == 0 ) { // child
            break;
        }
        // parent continues but will never create thread, and eventually exit
        std::cout << "New child process: " << ret << std::endl;
    }

    pthread_create(&mpd_id, NULL, mpd, &handles);
    pthread_create(&msd_id, NULL, msd, &handles);

    pthread_join(mpd_id, NULL);
    pthread_join(msd_id, NULL);

    return 0;
}

