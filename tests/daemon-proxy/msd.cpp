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

//~ pthread_t msd_tx_id;
//~ pthread_t msd_rx_id;

int g_num_devices = 0;
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
 * return bdf string from open device handle
 */
static std::string get_bdf_from_device_mgmt( xclDeviceHandle handle )
{
    size_t max_path_size = 256;
    char raw_path[max_path_size] = {0};
    xclGetSysfsPathMgmt(handle, "", "", raw_path, max_path_size);

    return std::string( raw_path ).substr( strlen("/sys/bus/pci/devices/0000:"), strlen("xx:xx.x") );
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

    g_num_devices = pt.count("device");

    // make a sorted device list using scan() results (dev_list) and ptree (pt),
    // make a std::pair()
    for( int i = 0; i < g_num_devices; i++ ) {
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

static void local_init(int local_dev_idx, int *handle)
{
    int fd;

    // how to map device index to renderD#?
    const char device_name[] = "/dev/dri/renderD128";
    if (handle == NULL) {
        perror("null handle");
        exit(1);
    }
    if ((fd = open(device_name,O_RDWR)) == -1) {
        perror("open");
        exit(1);
    }

    *handle = fd;
}


// example code to setup communication channel between vm and host
// tcp is being used here as example.
// cloud vendor should implements this function
static void msd_comm_init(int *handle)
{
    int sockfd, connfd, len;
    struct sockaddr_in servaddr, cli;

    // socket create and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket creation failed...");
        exit(1);
    }
    else
        printf("Socket successfully created..\n");
    bzero(&servaddr, sizeof(servaddr));

    // get PORT from filesystem
    std::ifstream file( "/var/lib/libvirt/filesystem_passthrough/host_port" );
    std::string host_port;
    std::getline(file, host_port);
    file.close();

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons( std::stoi( host_port.c_str() ) );

    // Binding newly created socket to given IP and verification
    if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
        perror("socket bind failed...");
        exit(1);
    }
    else
        printf("Socket successfully binded..\n");

    // Now server is ready to listen and verification
    if ((listen(sockfd, 5)) != 0) {
        perror("Listen failed...");
        exit(1);
    }
    else
        printf("Server listening..\n");
    len = sizeof(cli);

    while (1) {
        // Accept the data packet from client and verification
        connfd = accept(sockfd, (SA*)&cli, (socklen_t*)&len);
        if (connfd < 0) {
            perror("server acccept failed...");
            continue;
        } else {
            printf("server acccept the client...\n");
        }

        //In case there are multiple VMs created on the same host,
        //there should be hust one msd running on host, and multiple mpds
        //each of which runs on a VM. So there would be multiple tcp
        //connections established. each child here handles one connection
        //If we use udp, no children processes are required.
        if (!fork()) { //child
            close(sockfd);
            *handle = connfd;
            return;
        }
        //parent
        close(connfd);
        while(waitpid(-1,NULL,WNOHANG) > 0); /* clean up child processes */
    }
    //assume the server never exit.
    printf("Never happen!!\n");
    exit(100);
}

//~ int init( unsigned idx )
//~ {
    //~ xclDeviceInfo2 deviceInfo;
    //~ xclErrorStatus m_errinfo;
    //~ unsigned deviceIndex = 0;

    //~ msd_comm_init(&g_sock_fd);

    //~ // inside forked child device now

    //~ std::string virt_bdf_from_mpd = "xx:xx.x";
    //~ recv( g_sock_fd, (void *)(virt_bdf_from_mpd.c_str()), sizeof(virt_bdf_from_mpd), 0 );
    //~ std::cout << "virt bdf recv(): " << virt_bdf_from_mpd << std::endl;

    //~ // lookup device id of virt_bdf
    //~ std::vector<std::pair<std::string,std::string>>::iterator it;
    //~ for( it = master_dev_list.begin(); it != master_dev_list.end(); it++ ) {
        //~ if( it->second != virt_bdf_from_mpd )
            //~ break;

        //~ deviceIndex = std::distance( master_dev_list.begin(), it );
        //~ std::cout << "Found match at index: " << deviceIndex << " actual bdf: " << it->first << std::endl;
    //~ }

    //~ // before we assign a device to a socket, we must pair it, sent over
    //~ // wire from mpd in first message
    //~ xclDeviceHandle uHandle = xclOpenMgmt(deviceIndex, NULL, XCL_INFO);

    //~ if( !uHandle )
        //~ throw std::runtime_error("Failed to open mgmt device.");

    //~ if( xclGetDeviceInfo2(uHandle, &deviceInfo) ) {
        //~ throw std::runtime_error("Unable to obtain device information");
        //~ return -EBUSY;
    //~ }


    //~ struct s_handle devHandle = { uHandle };
    //~ pthread_create(&msd_tx_id, NULL, msd_tx, &devHandle);
    //~ pthread_create(&msd_rx_id, NULL, msd_rx, &devHandle);
//~ }

static void run(int local_fd, int comm_fd)
{
    /* inits for mailbox args handling */
    size_t prev_sz = INIT_BUF_SZ;
    struct drm_xocl_sw_mailbox args = { 0, 0, true, prev_sz, 0 };
    args.data = (uint32_t *)malloc(prev_sz);
    char client_message[MSG_SZ];
    uint32_t *pdata = args.data;

    fd_set rfds;
    FD_ZERO(&rfds);
    int ret = 0;
#define max(a,b) (a>b?a:b)
    for (;;) {
        FD_SET(local_fd, &rfds);
        FD_SET(comm_fd, &rfds);
        ret = select(max(comm_fd, local_fd)+1, &rfds, NULL, NULL, NULL);

        if(ret == -1) {
            break;
        }

        if(FD_ISSET(local_fd, &rfds)) {
            static int msd_tx_count = 0;
            std::cout << "              [MSD-TX]:" << msd_tx_count << ".1 MSD TX IOCTL \n";
            args.sz = prev_sz;
            //ret = xclMSD(handle, &args);
            ret = read( local_fd, &args, (sizeof(struct drm_xocl_sw_mailbox) + args.sz) );
            if( ret < 0 ) {
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
                //ret = xclMSD(handle, &args);
                ret = read( local_fd, &args, (sizeof(struct drm_xocl_sw_mailbox) + args.sz) );
                if( ret <= 0 ) {
                    std::cout << "              [MSD-TX]: second transfer failed, exiting.\n";
                    exit(1);
                }
            }

            std::cout << "              [MSD-TX]:" << msd_tx_count << ".2 send args over socket \n";
            send_args( comm_fd, &args );
            std::cout << "              [MSD-TX]:" << msd_tx_count << ".3 send data over socket \n";
            send_data( comm_fd, args.data, args.sz );
            std::cout << "              [MSD-TX]:" << msd_tx_count << " complete.\n";
            msd_tx_count++;
        }

        if(FD_ISSET(comm_fd, &rfds)) {
            static int msd_rx_count = 0;
            std::cout << "              [MSD-RX]:" << msd_rx_count << ".1 recv_args\n";
            if( recv_args( comm_fd, client_message, &args ) <= 0 )
                break;

            args.data = pdata; // must do this after recv_args
            args.is_tx = false; // must do this

            std::cout << "args.sz:" << args.sz << ", prev_sz:" << prev_sz << "\n";

            std::cout << "              [MSD-RX]:" << msd_rx_count << ".2 resize buffer\n";
            if( args.sz > prev_sz ) {
                std::cout << "args.sz(" << args.sz << ") > prev_sz(" << prev_sz << ") \n";
                resize_buffer( args.data, args.sz );
                prev_sz = args.sz;
            } else {
                std::cout << "don't need to resize buffer\n";
            }

            std::cout << "              [MSD-RX]:" << msd_rx_count << ".3 recv_data\n";
            if( recv_data( comm_fd, args.data, args.sz ) != 0 ) {
                std::cout << "bad retval from recv_data(), exiting.\n";
                exit(1);
            }

            std::cout << "              [MSD-RX]:" << msd_rx_count << ".4 xclMSD \n";
            //ret = xclMSD(handle, &args);
            ret = write( local_fd, &args, (sizeof(struct drm_xocl_sw_mailbox) + args.sz) );
            if( ret != 0 ) {
                std::cout << "          [MSD-RX]:" << msd_rx_count << " transfer error: " << strerror(errno) << std::endl;
                exit(1);
            }
            std::cout << "              [MSD-RX]:" << msd_rx_count << " complete.\n";

            msd_rx_count++;
        }
    }
}

int main( void )
{
    int comm_fd, local_fd;

    msd_comm_init(&comm_fd);
    local_init(0, &local_fd);
    run(local_fd, comm_fd);
    comm_fini(comm_fd);
    local_fini(local_fd);

    return 0;
}
