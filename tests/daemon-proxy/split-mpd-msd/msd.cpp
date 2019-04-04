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

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#define INIT_BUF_SZ 64

pthread_t msd_tx_id;
pthread_t msd_rx_id;

int g_sock_fd;

std::vector<std::pair<std::string,std::string>> master_dev_list;

std::string ptree_lookup_virt_bdf( std::string &str, boost::property_tree::ptree &pt )
{
    for( boost::property_tree::ptree::const_iterator it = pt.begin(); it != pt.end(); it++ ) {
        if( it->first == "device" ) {
            std::string act_bdf, virt_bdf;
            for( auto &subv : it->second ) {
                if( subv.first == "actual" )
                    act_bdf = subv.second.get_value<std::string>();

                if( subv.first == "virtual" )
                    virt_bdf = subv.second.get_value<std::string>();
            }
            if( str == act_bdf ) // string::compare to ignore the .0 vs .1 function at end
                return virt_bdf;
        }
    }
    return "";
}

/*
 * Host configures JSON file with actual and virtual BDF to be read by
 * this function. When called, all the devices are enumerated by using
 * pci_device_scanner::scan() to get the list of devices by index. Then
 * each entry from JSON is mapped to a device index.
 *
 * TODO: Optimize, compare list size w/ xclProbe, error check strings,
 */
bool create_device_list( void )
{
    // read json
    boost::property_tree::ptree pt;
    boost::property_tree::read_json("bdf_lut.json", pt);

    // scan to create device list dev_list
    int device_list_size = 1; // TODO, get size from json, compare to xclProbe()

    // make a sorted device list using scan() results (dev_list) and ptree (pt),
    // make a std::pair()
    for( int i = 0; i < device_list_size; i++ ) {
        // get actual bdf
        xclDeviceHandle hand = xclOpenMgmt(i, NULL, XCL_INFO);
        std::string act_bdf = get_bdf_from_device_mgmt( hand );
        xclClose( hand );

        // find this bdf in ptree, get its virtual bdf
        std::string virt_bdf = ptree_lookup_virt_bdf( act_bdf, pt );

        if( virt_bdf == "" )
            return false;

        master_dev_list.push_back( std::make_pair( act_bdf, virt_bdf ) );
    }

    return true;
}

void *msd_tx(void *handle_ptr)
{
    int xferCount = 0;
    int ret;
    struct s_handle *s_handle_ptr = (struct s_handle *)handle_ptr;
    xclDeviceHandle handle = s_handle_ptr->uDevHandle;
    size_t prev_sz = INIT_BUF_SZ;
    struct drm_xocl_sw_mailbox args = { 0, 0, true, prev_sz, 0 };
    args.data = (uint32_t *)malloc(prev_sz);

    std::cout << "              [XCLMGMT->XOCL Intercept ON (HAL)]\n";
    for( ;; ) {
        std::cout << "              [MSD-TX]:" << xferCount << ".1 MSD TX IOCTL \n";
        args.sz = prev_sz;
        ret = xclMSD(handle, &args);
        if( ret != 0 ) {
            if( errno != EMSGSIZE ) {
                std::cout << "              [MSD-TX]: transfer failed for other reason: errno:" << errno
                          << ", str: " << strerror(errno) << std::endl;
                exit(1);
            }

            if( resize_buffer( args.data, args.sz ) != 0 ) {
                std::cout << "              [MSD-TX]: resize_buffer() failed...exiting\n";
                exit(1);
            }

            prev_sz = args.sz; // store the newly alloc'd size
            ret = xclMSD(handle, &args);

            if( ret != 0 ) {
                std::cout << "              [MSD-TX]: second transfer failed, exiting.\n";
                exit(1);
            }
        }

        std::cout << "              [MSD-TX]:" << xferCount << ".2 send args over socket \n";
        send_args( g_sock_fd, &args );
        std::cout << "              [MSD-TX]:" << xferCount << ".3 send data over socket \n";
        send_data( g_sock_fd, args.data, args.sz );
        std::cout << "              [MSD-TX]:" << xferCount << " complete.\n";
        xferCount++;
    }
    std::cout << "              [MSD-TX]: exit.\n";
    xclClose(handle);
}

void *msd_rx(void *handle_ptr)
{
    int xferCount = 0;
    int ret;
    struct s_handle *s_handle_ptr = (struct s_handle *)handle_ptr;
    xclDeviceHandle handle = s_handle_ptr->uDevHandle;
    size_t prev_sz = INIT_BUF_SZ;
    struct drm_xocl_sw_mailbox args = { 0, 0, false, prev_sz, 0 };
    args.data = (uint32_t *)malloc(prev_sz);
    uint32_t *pdata = args.data;

    char client_message[MSG_SZ];

    for( ;; ) {
        std::cout << "              [MSD-RX]:" << xferCount << ".1 recv_args\n";
        if( recv_args( g_sock_fd, client_message, &args ) <= 0 )
            break;

        args.data = pdata; // must do this after recv_args
        args.is_tx = false; // must do this

        std::cout << "args.sz:" << args.sz << ", prev_sz:" << prev_sz << "\n";

        std::cout << "              [MSD-RX]:" << xferCount << ".2 resize buffer\n";
        if( args.sz > prev_sz ) {
            std::cout << "args.sz(" << args.sz << ") > prev_sz(" << prev_sz << ") \n";
            resize_buffer( args.data, args.sz );
            prev_sz = args.sz;
        } else {
            std::cout << "don't need to resize buffer\n";
        }

        std::cout << "              [MSD-RX]:" << xferCount << ".3 recv_data\n";
        if( recv_data( g_sock_fd, args.data, args.sz ) != 0 ) {
            std::cout << "bad retval from recv_data(), exiting.\n";
            exit(1);
        }

        std::cout << "              [MSD-RX]:" << xferCount << ".4 xclMSD \n";
        ret = xclMSD(handle, &args);
        if( ret != 0 ) {
            std::cout << "          [MSD-RX]:" << xferCount << " transfer error: " << strerror(errno) << std::endl;
            exit(1);
        }
        std::cout << "              [MSD-RX]:" << xferCount << " complete.\n";

        xferCount++;
    }
    std::cout << "              [MSD-RX]:" << xferCount << " exit.\n";
    xclClose(handle);
}

int init( unsigned idx )
{
    xclDeviceInfo2 deviceInfo;
    xclErrorStatus m_errinfo;
    unsigned deviceIndex = idx;

    xclDeviceHandle uHandle = xclOpenMgmt(deviceIndex, NULL, XCL_INFO);

    if( !uHandle )
        throw std::runtime_error("Failed to open mgmt device.");

    if( xclGetDeviceInfo2(uHandle, &deviceInfo) ) {
        throw std::runtime_error("Unable to obtain device information");
        return -EBUSY;
    }

    msd_comm_init(&g_sock_fd);

    struct s_handle devHandle = { uHandle };
    pthread_create(&msd_tx_id, NULL, msd_tx, &devHandle);
    pthread_create(&msd_rx_id, NULL, msd_rx, &devHandle);
}

void printHelp( void )
{
    std::cout << "Usage: <daemon_name> -d <device_index>" << std::endl;
    std::cout << "      '-d' is optional and will default to '0'" <<std::endl;
}

int main(int argc, char *argv[])
{
    if( !create_device_list() )
        exit( ENODEV );

    const int NumDevices = 1; // set to the number of devices on host
    for( int i = 0; i < NumDevices; i++ )
        init( i );

    // Block until thread killed
    pthread_join(msd_rx_id, NULL);

    // cleanup if thread returns
    pthread_cancel(msd_tx_id);
    comm_fini(g_sock_fd);
    //xclClose(uHandle);

    // Terminate the child process when the daemon completes
    exit(EXIT_SUCCESS);
}
