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


int main(void)
{
    // add echo 1 > sysfs/mailbox...ctrl to enable sw channel in mgmt
    //
    int uHandle = 0;
    uHandle = open("/dev/dri/renderD129", O_RDWR);
    if( uHandle < 0 )
        printf( "Error openning /dev/dri/renderD129\n" );
    
    int ret = -1;
    uint64_t flags = 0;
    u32 *buffer;
    buffer = (u32*)calloc( 64, sizeof(u32) );
    struct drm_xocl_sw_mailbox args_tx = { flags, buffer, true, 64 };
    printf( "args_tx.pData %p\n", args_tx.pData );

    // mgmt setup
    int mHandle = 0;
    mHandle = open("/dev/xclmgmt2560", O_RDWR);
    if( mHandle < 0 )
        printf ( "Error opening mgmt: /dev/xclmgmt2560\n" );


    printf( "[XOCL->XCLMGMT Intercept ON]\n" );

    for( ;; ) {
        int i;

        //struct mailbox_pkt *pkt = args_tx.pData;
        //for( i = 0; i < 8; i++ ) {
        //    pkt->body.msg_body.payload[i] = 2*i*i;
        //}

        args_tx.isTx = true;
        ret = ioctl(uHandle, DRM_IOCTL_XOCL_SW_MAILBOX, &args_tx);
        printf("[xocl-pkt]\n");
        if( ret < 0 )
            printf("user-transfer Errno: %s\n", strerror(errno));

        //for( i = 0; i < 8; i++ ) {
        //    printf( "pkt->body.msg_body.payload[] : %lu\n", pkt->body.msg_body.payload[i] );
        //}
        //

        args_tx.isTx = false;
        ret = ioctl(mHandle, XCLMGMT_IOCSWMAILBOXTRANSFER, &args_tx);
        printf("[mgmt-pkt]\n");
        if( ret < 0 )
            printf("mgmt-transfer Errno: %s\n", strerror(errno));
    }

    return 0;
}
