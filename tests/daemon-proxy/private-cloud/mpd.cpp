/*
 * ryan.radjabi@xilinx.com
 *
 * Private Cloud Management Proxy Daemon
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

std::string host_ip;
std::string host_port;
std::string host_id;

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

int main(void)
{
    int comm_fd, local_fd = -1;
    const int numDevs = xclProbe();

    if (numDevs <= 0)
        return -ENODEV;

    for (int i = 0; i < numDevs; i++) {

        int ret = fork();
        if (ret < 0) {
            std::cout << "Failed to create child process: " << errno << std::endl;
            exit(errno);
        }
        if (ret == 0) { // child

            /* Ugly way to get host_ip, host_port, and cloud token(host_id) */
            char c_id[256];
            xclMailboxUserGetID(i, c_id);
            std::string s_id = std::string( c_id );
            std::cout << "s_id : " << s_id << std::endl;
            size_t pos =  s_id.find(",");
            std::string rem = s_id.substr( pos+1, s_id.length() );
            host_ip = s_id.substr( 0, pos );
            pos =  rem.find(",");
            host_port = rem.substr( 0, pos );
            rem = rem.substr( pos+1, rem.length()-1 );
            host_id = rem.substr( 0, rem.length()-1 );

            mpd_comm_init(&comm_fd);

            /* handshake to MSD by sending cloud token id */
            int64_t i64_host_id = std::stoi(host_id);
            int64_t cloud_token = htonl(i64_host_id);
            char *data = (char*)&cloud_token;
            std::cout << "cloud_token = std::stoi(host_id) " << cloud_token <<std::endl;
            if( write( comm_fd, data, sizeof(cloud_token) ) == -1 ) {
                std::cout << "Handshake comm_write token failed\n";
                exit(1);
            }

            local_fd = xclMailbox( i );
            break;
        }
        // parent continues but will never create thread, and eventually exit
        std::cout << "New child process: " << ret << std::endl;
    }

    if ((comm_fd > 0) && (local_fd > 0)) {
        // run until daemon is killed
        mailbox_daemon(local_fd, comm_fd, "[MPD]");

        // cleanup when stopped
        comm_fini(comm_fd);
        close(local_fd);
    }
    return 0;
}

