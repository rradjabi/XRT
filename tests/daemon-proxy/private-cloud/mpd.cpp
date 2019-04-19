/*
 * ryan.radjabi@xilinx.com
 *
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

#define INIT_BUF_SZ 64000000


// example code to setup communication channel between vm and host
// tcp is being used here as example.
// cloud vendor should implements this function
static void mpd_comm_init(int *handle)
{
    int sockfd;
    struct sockaddr_in servaddr;

    // socket create and varification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket creation failed...");
        exit(1);
    }
    else
        printf("Socket successfully created..\n");
    bzero(&servaddr, sizeof(servaddr));

    // get host IP from filesystem passthrough
    std::ifstream file( "/tmp/host_files/host_ip" );
    std::string host_ip;
    std::getline(file, host_ip);
    file.close();
    file.open( "/tmp/host_files/host_port" );
    std::string host_port;
    std::getline(file, host_port);
    file.close();

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr( host_ip.c_str() );
    servaddr.sin_port = htons( std::stoi( host_port.c_str() ) );

    // connect the client socket to server socket
    if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
        perror("connection with the server failed...");
        exit(1);
    }
    else {
        printf("connected to the server..\n");
    }

    *handle = sockfd;
}

void run(int local_fd, int comm_fd)
{
    /* inits for mailbox args handling */
    size_t prev_sz = INIT_BUF_SZ;
    struct drm_xocl_sw_mailbox args = { prev_sz, 0, true, 0, 0 };
    args.data = (uint32_t *)malloc(prev_sz);
    char reply[MSG_SZ];
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
            static int mpd_tx_count = 0;
            /* read args from mailbox */
            std::cout << "[MPD-TX]:" << mpd_tx_count << ".1 MPD TX IOCTL \n";
            if( read_from_func( local_fd, &args, prev_sz ) )
                exit(errno);
/* read from userpf */
//-            args.is_tx = true;
//-            args.sz = prev_sz;
//-            ret = read( local_fd, &args, (sizeof(struct drm_xocl_sw_mailbox) + args.sz) );
//-            /* DEBUG, compare ret with args.sz */
//-            if( ret != 0 )
//-                std::cout << "read errno: " << errno << std::endl;
//-            std::cout << "[MPD-TX]: read request sz: " << (sizeof(struct drm_xocl_sw_mailbox)+prev_sz)
//-                      << ", read sz: " << ret
//-                      << ", args.sz: " << args.sz
//-                      << ", sizeof(struct)+payload: " << (sizeof(struct drm_xocl_sw_mailbox) + args.sz) << std::endl;
//-
//-            /* resize buffer and re-read if necessary */
//-            if( ret <= 0 ) {
//-                std::cout << "#1\n";
//-                if( errno != EMSGSIZE ) {
//-                    std::cout << "[MPD-TX]: transfer failed for other reason\n";
//-                    exit(errno);
//-                }
//-                std::cout << "#2\n";
//-                if( resize_buffer( args.data, args.sz ) != 0 ) {
//-                    std::cout << "#2.1\n";
//-                    std::cout << "[MPD-TX]: resize_buffer() failed...exiting\n";
//-                    exit(1);
//-                }
//-                std::cout << "#3\n";
//-                prev_sz = args.sz; // store the newly alloc'd size
//-                ret = read( local_fd, &args, (sizeof(struct drm_xocl_sw_mailbox) + args.sz) );//xclMPD(handle, &args);
//-                if( ret != 0 )
//-                    std::cout << "read errno: " << errno << std::endl;
//-                std::cout << "[MPD-TX]: read request sz: " << (sizeof(struct drm_xocl_sw_mailbox)+prev_sz)
//-                          << ", read sz: " << ret
//-                          << ", args.sz: " << args.sz
//-                          << ", sizeof(struct)+payload: " << (sizeof(struct drm_xocl_sw_mailbox) + args.sz) << std::endl;
//-
//-                if( ret < 0 ) {
//-                    std::cout << "[MPD-TX]: second transfer failed, exiting.\n";
//-                    exit(errno);
//-                }
//-            }
/* read from userpf complete */

            /* write args to comm socket */
            std::cout << "[MPD-TX]:" << mpd_tx_count << ".2 send args over socket\n";
            write_args( comm_fd, &args );

            /* write data to comm socket */
            std::cout << "[MPD-TX]:" << mpd_tx_count << ".3 send payload over socket\n";
            write_data( comm_fd, args.data, args.sz );

            std::cout << "[MPD-TX]:" << mpd_tx_count << " complete.\n";
            mpd_tx_count++;
        }

        if(FD_ISSET(comm_fd, &rfds)) {
            static int mpd_rx_count = 0;
            std::cout << "[MPD-RX]:" << mpd_rx_count << ".1 recv_args\n";
            if( read_args( comm_fd, reply, &args ) <= 0 )
                break;

            args.data = pdata; // must do this after recv_args
            args.is_tx = false;

            std::cout << "[MPD-RX]:" << mpd_rx_count << ".2 resize buffer\n";
            if( args.sz > prev_sz ) {
                std::cout << "args.sz(" << args.sz << ") > prev_sz(" << prev_sz << ") \n";
                resize_buffer( args.data, args.sz );
                prev_sz = args.sz;
            } else {
                std::cout << "don't need to resize buffer\n";
            }

            std::cout << "[MPD-RX]:" << mpd_rx_count << ".3 recv_data \n";
            if( read_data( comm_fd, args.data, args.sz ) != 0 ) {
                std::cout << "bad retval from recv_data(), exiting.\n";
                exit(1);
            }

            std::cout << "[MPD-RX]:" << mpd_rx_count << ".4 xclMPD \n";
            //~ ret = xclMPD(handle, &args);
/* write to userpf */
//-            ret = write( local_fd, &args, (sizeof(struct drm_xocl_sw_mailbox) + args.sz) );
//-            if( ret < 0 ) {
//-                std::cout << "[MPD-RX]: transfer error: " << strerror(errno) << std::endl;
//-                exit(errno);
//-            }
/* write to userpf complete */
            if( write_to_func( local_fd, &args ) )
                exit(errno);
            std::cout << "[MPD-RX]:" << mpd_rx_count << " complete.\n";

            mpd_rx_count++;
        }
    }
}

int main(void)
{
    int comm_fd, local_fd = -1;
    const int numDevs = xclProbe();

    if (numDevs <= 0)
        return -ENODEV;

    //~ for (int i = 0; i < numDevs; i++) {

        //~ int ret = fork();
        //~ if (ret < 0) {
            //~ std::cout << "Failed to create child process: " << errno << std::endl;
            //~ exit(errno);
        //~ }
        //~ if (ret == 0) { // child
            //~ mpd_comm_init(&comm_fd);
            //~ local_fd = xclMailbox(i);
            //~ break;
        //~ }
        //~ // parent continues but will never create thread, and eventually exit
        //~ std::cout << "New child process: " << ret << std::endl;
    //~ }
    mpd_comm_init(&comm_fd);
    local_fd = xclMailbox(0);

    if ((comm_fd > 0) && (local_fd > 0)) {
        // run until daemon is killed
        run(local_fd, comm_fd);

        // cleanup when stopped
        comm_fini(comm_fd);
        close(local_fd);
    }
    return 0;
}

