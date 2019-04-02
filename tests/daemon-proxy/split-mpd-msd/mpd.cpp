/*
 * ryan.radjabi@xilinx.com
 *
 * Reference for daemonization: https://gist.github.com/alexdlaird/3100f8c7c96871c5b94e
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
#include "common.h"

#define INIT_BUF_SZ 64

xclDeviceHandle uHandle;
pthread_t mpd_tx_id;
pthread_t mpd_rx_id;

int g_sock_fd;

void *mpd_tx(void *handle_ptr)
{
    int xferCount = 0;
    int ret;
    struct s_handle *s_handle_ptr = (struct s_handle *)handle_ptr;
    xclDeviceHandle handle = s_handle_ptr->uDevHandle;
    size_t prev_sz = INIT_BUF_SZ;
    struct drm_xocl_sw_mailbox args = { 0, 0, true, prev_sz, 0 };
    args.data = (uint32_t *)malloc(prev_sz);

    std::cout << "[XOCL->XCLMGMT Intercept ON (HAL)]\n";
    for( ;; ) {
        std::cout << "[MPD-TX]:" << xferCount << ".1 MPD TX IOCTL \n";
        args.sz = prev_sz;
        ret = xclMPD(handle, &args);
        if( ret != 0 ) {
            if( errno != EMSGSIZE ) {
                std::cout << "[MPD-TX]: transfer failed for other reason\n";
                exit(1);
            }

            if( resize_buffer( args.data, args.sz ) != 0 ) {
                std::cout << "[MPD-TX]: resize_buffer() failed...exiting\n";
                exit(1);
            }

            prev_sz = args.sz; // store the newly alloc'd size
            ret = xclMPD(handle, &args);

            if( ret != 0 ) {
                std::cout << "[MPD-TX]: second transfer failed, exiting.\n";
                exit(1);
            }
        }
        
        std::cout << "[MPD-TX]:" << xferCount << ".2 send args over socket\n";
        send_args( g_sock_fd, &args );
        std::cout << "[MPD-TX]:" << xferCount << ".3 send payload over socket\n";
        send_data( g_sock_fd, args.data, args.sz );

        std::cout << "[MPD-TX]:" << xferCount << " complete.\n";
        xferCount++;

    }
    xclClose(handle);
    std::cout << "[MPD-TX] exit.\n";
}

void *mpd_rx(void *handle_ptr)
{
    int xferCount = 0;
    int ret;
    struct s_handle *s_handle_ptr = (struct s_handle *)handle_ptr;
    xclDeviceHandle handle = s_handle_ptr->uDevHandle;
    size_t prev_sz = INIT_BUF_SZ;
    struct drm_xocl_sw_mailbox args = { 0, 0, false, prev_sz, 0 };
    args.data = (uint32_t *)malloc(prev_sz);
    uint32_t *pdata = args.data;

    int sock, read_size;
    char reply[MSG_SZ];

    for( ;; ) {
        std::cout << "[MPD-RX]:" << xferCount << ".1 recv_args\n";
        if( recv_args( g_sock_fd, reply, &args ) <= 0 )
            break;
        
        args.data = pdata; // must do this after recv_args
        args.is_tx = false;

        
        std::cout << "[MPD-RX]:" << xferCount << ".2 resize buffer\n";
        if( args.sz > prev_sz ) {
            std::cout << "args.sz(" << args.sz << ") > prev_sz(" << prev_sz << ") \n";
            resize_buffer( args.data, args.sz );
            prev_sz = args.sz;
        } else {
            std::cout << "don't need to resize buffer\n";
        }

        std::cout << "[MPD-RX]:" << xferCount << ".3 recv_data \n";
        if( recv_data( g_sock_fd, args.data, args.sz ) != 0 ) {
            std::cout << "bad retval from recv_data(), exiting.\n";
            exit(1);
        }

        std::cout << "[MPD-RX]:" << xferCount << ".4 xclMPD \n";
        ret = xclMPD(handle, &args);
        if( ret != 0 ) {
            std::cout << "[MPD-RX]: transfer error: " << strerror(errno) << std::endl;
            exit(1);
        }
        std::cout << "[MPD-RX]:" << xferCount << " complete.\n";

        xferCount++;
    }
    xclClose(handle);
    std::cout << "[MPD-RX] exit.\n";
}

int init( unsigned idx )
{
    xclDeviceInfo2 deviceInfo;
    unsigned deviceIndex = idx;

    if( deviceIndex >= xclProbe() ) {
        throw std::runtime_error("Cannot find specified device index");
        return -ENODEV;
    }

    uHandle = xclOpen(deviceIndex, NULL, XCL_INFO);
    struct s_handle devHandle = { uHandle };

    if( xclGetDeviceInfo2(uHandle, &deviceInfo) ) {
        throw std::runtime_error("Unable to obtain device information");
        return -EBUSY;
    }

    mpd_comm_init( &g_sock_fd );

    pthread_create(&mpd_rx_id, NULL, mpd_rx, &devHandle);
    pthread_create(&mpd_tx_id, NULL, mpd_tx, &devHandle);
}

void printHelp( void )
{
    std::cout << "Usage: <daemon_name> -d <device_index>" << std::endl;
    std::cout << "      '-d' is optional and will default to '0'" <<std::endl;
}

int main(int argc, char *argv[])
{
    unsigned index = 0;
    int c;

    while ((c = getopt(argc, argv, "d:h:")) != -1)
    {
        switch (c)
        {
        case 'd':
            index = std::atoi(optarg);
            break;
        case 'h':
            printHelp();
            return 0;
        default:
            printHelp();
            return -1;
        }
    }

    init(index);
  
    // Enter daemon loop
    pthread_join(mpd_tx_id, NULL);

    // cleanup if thread returns
    pthread_cancel(mpd_rx_id);
    comm_fini(g_sock_fd);
    //xclClose(uHandle);

    // Terminate the child process when the daemon completes
    exit(EXIT_SUCCESS);
}

