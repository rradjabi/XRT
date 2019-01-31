/*
 * http://www.netzmafia.de/skripten/unix/linux-daemon-howto.html
 */
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


int main(void)
{
    // add echo 1 > sysfs/mailbox...ctrl to enable sw channel in mgmt

    int mUserHandle = 0;
    mUserHandle = open("/dev/dri/renderD129", O_RDWR);
    if( mUserHandle < 0 )
        printf( "Error openning /dev/dri/renderD129\n" );
    
    uint64_t flags = 0;
    void *ptr;
    uint64_t *buffer;
    buffer = malloc( 64 * 2 * sizeof(uint64_t) );
    ptr = buffer;
    struct drm_xocl_sw_mailbox args = { flags, ptr, 64 };
    //struct drm_xocl_sw_mailbox args = { 0, 0, 0 };
    printf( "ioctl return: %d\n", ioctl(mUserHandle, DRM_IOCTL_XOCL_SW_MAILBOX, &args) );
    printf( "Errno: %s\n", strerror(errno));

    return 0;
}
