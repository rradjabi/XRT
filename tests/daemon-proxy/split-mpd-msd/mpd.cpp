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
#include "sockets/socket.h"

#define INIT_BUF_SZ 64

char CMD_TEST[] = "CMD_TEST\0";
char CMD_COMPLETE[] = "CMD_COMPLETE\0";
char CMD_DATA_READY[] = "CMD_DATA_READY\0";
char CMD_RESPONSE[] = "CMD_RESPONSE\0";


xclDeviceHandle uHandle;
pthread_t mpd_tx_id;
pthread_t msd_rx_id;

struct s_handle {
        xclDeviceHandle uDevHandle;
};

struct drm_xocl_sw_mailbox {
    uint64_t flags;
    uint32_t *data;
    bool is_tx;
    size_t sz;
    uint64_t id;
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

void *mpd_tx(void *handle_ptr)
{
    int xferCount = 0;
    int ret;
    struct s_handle *s_handle_ptr = (struct s_handle *)handle_ptr;
    xclDeviceHandle handle = s_handle_ptr->uDevHandle;
    size_t prev_sz = INIT_BUF_SZ;
    struct drm_xocl_sw_mailbox args = { 0, 0, true, prev_sz, 0 };
    args.data = (uint32_t *)malloc(prev_sz);

    int sock, read_size;
    socket_client_init( sock, 8888 );
    char message[MSG_SZ], server_reply[MSG_SZ];
    while(1)
    {
        printf("press any key: ");
        scanf("%s" , message);
        send( sock, CMD_TEST, sizeof(CMD_TEST), 0 );
        int read_size = recv( sock, server_reply, MSG_SZ, 0 );
        if( read_size == 0 )
                break;

        puts("Server reply :");
        puts(server_reply);
        break;
    }

    std::cout << "[XOCL->XCLMGMT Intercept ON (HAL)]\n";
    for( ;; ) {
        std::cout << "MPD-dbg-00\n";
        args.is_tx = true;
        args.sz = prev_sz;
        ret = xclMPD(handle, &args);
        std::cout << "MPD-dbg-01\n";
        if( ret != 0 ) {
            // sw channel xfer error 
            if( errno == EMSGSIZE ) {
                // buffer was of insufficient size, resizing
                if( resize_buffer( args.data, args.sz ) != 0 ) {
                    std::cout << "MPD: resize_buffer() failed...exiting\n";
                    exit(1);
                }
                prev_sz = args.sz; // store the newly alloc'd size
                ret = xclMPD(handle, &args);
            } else {
                std::cout << "MPD: transfer failed for other reason\n";
                exit(1);
            }

            if( ret != 0 ) {
                std::cout << "MPD: second transfer failed, exiting.\n";
                exit(1);
            }
        }
        std::cout << "[MPD-TX]\n";

        send( sock, CMD_DATA_READY, sizeof(CMD_DATA_READY), 0 );
	    read_size = recv( sock, server_reply, MSG_SZ, 0 );
        if( read_size == 0 )
            break;

        puts( "server reply: " );
        puts( server_reply );

        if( strcmp(server_reply, CMD_COMPLETE) ) {
            std::cout << "MPD: unexpected server reply: " << server_reply << std::endl;
            break;
        }
        std::cout << "MPD-dbg-10\n";
    }
    std::cout << "MPD exit.\n";
}

void *msd_rx(void *handle_ptr)
{
    int xferCount = 0;
    int ret;
    struct s_handle *s_handle_ptr = (struct s_handle *)handle_ptr;
    xclDeviceHandle handle = s_handle_ptr->uDevHandle;
    size_t prev_sz = INIT_BUF_SZ;
    struct drm_xocl_sw_mailbox args = { 0, 0, true, prev_sz, 0 };
    args.data = (uint32_t *)malloc(prev_sz);

    int sock, client_sock, read_size;
    char client_message[MSG_SZ];
    socket_server_init( sock, client_sock, 8888 );

    //Receive a message from client
    while( (read_size = recv(client_sock , client_message , MSG_SZ, 0)) > 0 ) {
        printf("                recv: %s\n", client_message);
        send( client_sock, CMD_RESPONSE, sizeof(CMD_RESPONSE), 0 );
        break;
    }

    if(read_size == 0) {
        puts("                  Client disconnected");
        fflush(stdout);
    } else if(read_size == -1) {
        perror("                recv failed");
    }

    for( ;; ) {
        // Receive a message from MPD client
        read_size = recv( client_sock, client_message, MSG_SZ, 0 ); // blocking
	if( strcmp(client_message, CMD_DATA_READY) ) {
		std::cout << "MSD: unexpected client reply: " << client_message << std::endl;
		break;
	}

        args.is_tx = false;
        ret = xclMSD(handle, &args);
        if( ret != 0 ) {
            std::cout << "MSD: transfer error: " << strerror(errno) << std::endl;
            exit(1);
        }
        std::cout << "[MSD-RX]: " << xferCount << std::endl;
        send( client_sock, CMD_COMPLETE, sizeof(CMD_COMPLETE), 0 );
        xferCount++;
    }
    std::cout << "MSD exit.\n";
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

    pthread_create(&msd_rx_id, NULL, msd_rx, &devHandle);
    usleep( 50000 ); // 50 ms sleep for socket to bind
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
 
    // Define variables
    pid_t pid, sid;
 
    // Fork the current process
    pid = fork();
    // The parent process continues with a process ID greater than 0
    if(pid > 0)
    {
        exit(EXIT_SUCCESS);
    }
    // A process ID lower than 0 indicates a failure in either process
    else if(pid < 0)
    {
        exit(EXIT_FAILURE);
    }
    // The parent process has now terminated, and the forked child process will continue
    // (the pid of the child process was 0)
 
    // Since the child process is a daemon, the umask needs to be set so files and logs can be written
    umask(0);
 
    // Open system logs for the child process
    openlog("daemon-named", LOG_NOWAIT | LOG_PID, LOG_USER);
    syslog(LOG_NOTICE, "Successfully started daemon-name");
 
    // Generate a session ID for the child process
    sid = setsid();
    // Ensure a valid SID for the child process
    if(sid < 0)
    {
        // Log failure and exit
        syslog(LOG_ERR, "Could not generate session ID for child process");
 
        // If a new session ID could not be generated, we must terminate the child process
        // or it will be orphaned
        exit(EXIT_FAILURE);
    }
 
    // Change the current working directory to a directory guaranteed to exist
    if((chdir("/")) < 0)
    {
        // Log failure and exit
        syslog(LOG_ERR, "Could not change working directory to /");
 
        // If our guaranteed directory does not exist, terminate the child process to ensure
        // the daemon has not been hijacked
        exit(EXIT_FAILURE);
    }
 
    // A daemon cannot use the terminal, so close standard file descriptors for security reasons
//    close(STDIN_FILENO);
//    close(STDOUT_FILENO);
//    close(STDERR_FILENO);
 
    // Daemon-specific intialization should go here
    const int SLEEP_INTERVAL = 5;

    init(index);
  
    // Enter daemon loop
    pthread_join(msd_rx_id, NULL);
    pthread_join(mpd_tx_id, NULL);

    // Close system logs for the child process
    xclClose(uHandle);
    syslog(LOG_NOTICE, "Stopping daemon-name");
    closelog();
 
    // Terminate the child process when the daemon completes
    exit(EXIT_SUCCESS);
}

