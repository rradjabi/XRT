/*
 * http://www.netzmafia.de/skripten/unix/linux-daemon-howto.html
 */
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <sys/ioctl.h>
#include <libdrm/drm.h>
#include <pthread.h>

#include "xocl_ioctl.h"
#include "mgmt-ioctl.h"

#define PACKET_SIZE 16 /* Number of DWORD. */
typedef uint32_t u32;
typedef uint64_t u64;

struct mailbox_pkt {
    struct {
        u32     type;
        u32     payload_size;
    } hdr;
    union {
        u32     data[PACKET_SIZE - 2];
        struct {
            u64 msg_req_id;
            u32 msg_flags;
            u32 msg_size;
            u32 payload[0];
        } msg_start;
        struct {
            u32 payload[0];
        } msg_body;
    } body;
};

struct handles {
    int userHandle;
    int mgmtHandle;
};

void *sw_mailbox_chan_xocl_to_xclmgmt(void *handles_void_ptr)
{
    int ret;
    uint64_t flags = 0;
    u32 *buffer;
    buffer = (u32*)calloc( 64, sizeof(u32) );
    struct drm_xocl_sw_mailbox args = { flags, buffer, true, 64 };
    struct handles *sHandles = (struct handles *)handles_void_ptr;
    printf( "args.pData %p\n", args.pData );

    printf( "[XOCL->XCLMGMT Intercept ON]\n" );
    for( ;; ) {
        args.isTx = true;
        ret = ioctl(sHandles->userHandle, DRM_IOCTL_XOCL_SW_MAILBOX_TX, &args);
        printf("[xocl-pkt-tx]\n");
        if( ret < 0 ) {
            printf("user-transfer Errno: %s\n", strerror(errno));
            return;
        }

        args.isTx = false;
        ret = ioctl(sHandles->mgmtHandle, XCLMGMT_IOCSWMAILBOXRX, &args);
        printf("[mgmt-pkt-rx]\n");
        if( ret < 0 ) {
            printf("mgmt-transfer Errno: %s\n", strerror(errno));
            return;
        }
    }
}

void *sw_mailbox_chan_xclmgmt_to_xocl(void *handles_void_ptr)
{
    int ret;
    uint64_t flags = 0;
    u32 *buffer;
    buffer = (u32*)calloc( 64, sizeof(u32) );
    struct drm_xocl_sw_mailbox args = { flags, buffer, true, 64 };
    struct handles *sHandles = (struct handles *)handles_void_ptr;
    printf( "args.pData %p\n", args.pData );

    printf( "[XCLMGMT->XOCL Intercept ON]\n" );
    for( ;; ) {
        args.isTx = true;
        ret = ioctl(sHandles->mgmtHandle, XCLMGMT_IOCSWMAILBOXTX, &args);
        printf("                [mgmt-pkt-tx]\n");
        if( ret < 0 ) {
            printf("user-transfer Errno: %s\n", strerror(errno));
            return;
        }

        args.isTx = false;
        ret = ioctl(sHandles->userHandle, DRM_IOCTL_XOCL_SW_MAILBOX_RX, &args);
        printf("                [xocl-pkt-rx]\n");
        if( ret < 0 ) {
            printf("mgmt-transfer Errno: %s\n", strerror(errno));
            return;
        }
    }
}

int main(void)
{
    printf( "size of uint64_t: %lu\n", sizeof(uint64_t) );
    // add echo 1 > sysfs/mailbox...ctrl to enable sw channel in mgmt
    //
    int uHandle = 0;
    uHandle = open("/dev/dri/renderD129", O_RDWR);
    if( uHandle < 0 )
        printf( "Error openning /dev/dri/renderD129\n" );

//    int uH2 = 0;
//    uH2 = open("/dev/dri/renderD129", O_RDWR);
//    if( uH2< 0 )
//        printf( "Error openning uH2 /dev/dri/renderD129\n" );
    
    // mgmt setup
    int mHandle = 0;
    mHandle = open("/dev/xclmgmt2560", O_RDWR);
    if( mHandle < 0 )
        printf ( "Error opening mgmt: /dev/xclmgmt2560\n" );

    struct handles hands = { uHandle, mHandle };

    pthread_t xocl_to_xclmgmt_id;
    pthread_create(&xocl_to_xclmgmt_id, NULL, sw_mailbox_chan_xocl_to_xclmgmt, &hands);

    pthread_t xclmgmt_to_xocl_id;
    pthread_create(&xclmgmt_to_xocl_id, NULL, sw_mailbox_chan_xclmgmt_to_xocl, &hands);

    pthread_join(xocl_to_xclmgmt_id, NULL);
    pthread_join(xclmgmt_to_xocl_id, NULL);

    return 0;
}
