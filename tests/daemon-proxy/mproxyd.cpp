/*
 * ryan.radjabi@xilinx.com
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdexcept>
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

#include "xclhal2.h"

#define PACKET_SIZE 64 
typedef uint32_t u32;

/*
 * struct drm_xocl_sw_mailbox *args
 **/
struct drm_xocl_sw_mailbox {
    uint64_t flags;
    void *pData;
    bool isTx;
    uint64_t sz;
};

struct s_handle {
    xclDeviceHandle xclDevHandle;
};

void *xcl_sw_chan_xocl_to_xclmgmt(void *s_handle_ptr)
{
    int ret;
    uint64_t flags = 0;
    struct s_handle *handle = (struct s_handle*)s_handle_ptr;
    u32 *buffer;
    buffer = (u32*)calloc( (PACKET_SIZE/sizeof(u32)), sizeof(u32) );
    struct drm_xocl_sw_mailbox args = { flags, buffer, true, PACKET_SIZE };

    printf( "[XOCL->XCLMGMT Intercept ON (HAL)]\n" );
    for( ;; ) {
        args.isTx = true;
        ret = xclDaemonUserpf(handle->xclDevHandle, &args);
        if( ret != 0 ) {
            printf("user-transfer Errno: %s\n", strerror(errno));
            break;
        }
        printf("[XOCL-PKT-TX]\n");

        args.isTx = false;
        ret = xclDaemonMgmtpf(handle->xclDevHandle, &args);
        if( ret != 0 ) {
            printf("mgmt-transfer Errno: %s\n", strerror(errno));
            break;
        }
        printf("[MGMT-PKT-RX]\n");
    }
}

void *xcl_sw_chan_xclmgmt_to_xocl(void *s_handle_ptr)
{
    int ret;
    uint64_t flags = 0;
    struct s_handle *handle = (struct s_handle*)s_handle_ptr;
    u32 *buffer;
    buffer = (u32*)calloc( (PACKET_SIZE/sizeof(u32)), sizeof(u32) );
    struct drm_xocl_sw_mailbox args = { flags, buffer, true, PACKET_SIZE };

    printf( "[XCLMGMT->XOCL Intercept ON (HAL)]\n" );
    for( ;; ) {
        args.isTx = true;
        ret = xclDaemonMgmtpf(handle->xclDevHandle, &args);
        if( ret != 0 ) {
            printf("mgmt-transfer Errno: %s\n", strerror(errno));
            break;
        }
        printf("                [MGMT-PKT-TX]\n");

        args.isTx = false;
        ret = xclDaemonUserpf(handle->xclDevHandle, &args);
        if( ret != 0 ) {
            printf("user-transfer Errno: %s\n", strerror(errno));
            break;
        }
        printf("                [XOCL-PKT-RX]\n");
    }
}


int main(void)
{
    // add echo 1 > sysfs/mailbox...ctrl to enable sw channel in mgmt
    
    xclDeviceInfo2 deviceInfo;
    unsigned deviceIndex = 0;
    xclDeviceHandle handle;

    if( deviceIndex >= xclProbe() ) {
        throw std::runtime_error("Cannot find specified device index");
        return -ENODEV;
    }

    handle = xclOpen(deviceIndex, NULL, XCL_INFO);
    struct s_handle devHandle = { handle };

    if( xclGetDeviceInfo2(handle, &deviceInfo) ) {
        throw std::runtime_error("Unable to obtain device information");
        return -EBUSY;
    }

    pthread_t xcl_xocl_to_xclmgmt_id;
    pthread_create(&xcl_xocl_to_xclmgmt_id, NULL, xcl_sw_chan_xocl_to_xclmgmt, &handle);

    pthread_t xcl_xclmgmt_to_xocl_id;
    pthread_create(&xcl_xclmgmt_to_xocl_id, NULL, xcl_sw_chan_xclmgmt_to_xocl, &handle);

    pthread_join(xcl_xocl_to_xclmgmt_id, NULL);
    pthread_join(xcl_xclmgmt_to_xocl_id, NULL);

    return 0;
}
