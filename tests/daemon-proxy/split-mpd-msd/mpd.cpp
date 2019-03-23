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
    socket_client_init( sock, PORT_MPD_TO_MSD );
    char message[MSG_SZ], server_reply[MSG_SZ];
    
    //~ while(1) { /* some handshake or synchronization can go here */
        //~ printf("press any key: ");
        //~ scanf("%s" , message);
        //~ send( sock, CMD_TEST, sizeof(CMD_TEST), 0 );
        //~ int read_size = recv( sock, server_reply, MSG_SZ, 0 );
        //~ if( read_size == 0 )
            //~ break;

        //~ puts("Server reply :");
        //~ puts(server_reply);
        //~ break;
    //~ }

    std::cout << "[XOCL->XCLMGMT Intercept ON (HAL)]\n";
    for( ;; ) {
        std::cout << "MPD-TX (1) MPD TX IOCTL \n";
        args.sz = prev_sz;
        ret = xclMPD(handle, &args);
        if( ret != 0 ) {
            if( errno != EMSGSIZE ) {
                std::cout << "MPD: transfer failed for other reason\n";
                exit(1);
            }

            if( resize_buffer( args.data, args.sz ) != 0 ) {
                std::cout << "MPD: resize_buffer() failed...exiting\n";
                exit(1);
            }

            prev_sz = args.sz; // store the newly alloc'd size
            ret = xclMPD(handle, &args);

            if( ret != 0 ) {
                std::cout << "MPD: second transfer failed, exiting.\n";
                exit(1);
            }
        }
        std::cout << "[MPD-TX]\n";
        
        std::cout << "MPD-TX (2) write payload and args to file \n";
        write_data( &args, "/tmp/mpd_tx_msg_bin" );
        write_args( &args, "/tmp/mpd_tx_msg_args" );
        
        std::cout << "MPD-TX (3) send DATA_READY \n";
        send( sock, CMD_DATA_READY, sizeof(CMD_DATA_READY), 0 );

        std::cout << "MPD-TX (4) Wait for COMPLETE \n";
        read_size = recv( sock, server_reply, MSG_SZ, 0 );
        
        std::cout << "MPD-TX: reply: " << server_reply << ", read_size = " << read_size << std::endl;
        if( read_size == 0 )
            break;

        if( strcmp(server_reply, CMD_COMPLETE) ) {
            std::cout << "MPD-TX: unexpected server reply: " << server_reply << std::endl;
            break;
        }
    }
    std::cout << "MPD-TX exit.\n";
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

    int sock, read_size;
    socket_client_init( sock, PORT_MSD_TO_MPD );
    char message[MSG_SZ], server_reply[MSG_SZ];

    //~ /* Some sort of handshake is here */
    //~ //Receive a message from client
    //~ while( (read_size = recv(sock, server_reply, MSG_SZ, 0)) > 0 ) {
        //~ printf("                recv: %s\n", server_reply);
        //~ send( sock, CMD_RESPONSE, sizeof(CMD_RESPONSE), 0 );
        //~ break;
    //~ }

    for( ;; ) {
        std::cout << "MPD-RX (1) Wait for DATA_READY \n";
        read_size = recv( sock, server_reply, MSG_SZ, 0 ); // blocking
        std::cout << "MPD-RX: reply: " << server_reply << ", read_size = " << read_size << std::endl;
        if( read_size == 0 )
            break;

        if( strcmp(server_reply, CMD_DATA_READY) ) {
            std::cout << "MPD-RX: unexpected reply: " << server_reply << std::endl;
            break;
        }
        
        std::cout << "MPD-RX (2) get args and payload from file, they have been scp to by MSD \n";
        read_args( &args, "/tmp/mpd_rx_msg_args" );
        read_data( args.data, args.sz, "/tmp/mpd_rx_msg_bin" ); // when do we call resize_buffer()? 

        std::cout << "MPD-RX (3) resize_buffer \n";
        resize_buffer( args.data, args.sz );

        std::cout << "MPD-RX (4) xclMPD \n";
        ret = xclMPD(handle, &args);
        if( ret != 0 ) {
            std::cout << "MPD-RX: transfer error: " << strerror(errno) << std::endl;
            exit(1);
        }
        std::cout << "[MPD-RX]: " << xferCount << std::endl;
        
        std::cout << "MPD-RX (5) send COMPLETE \n";
        send( sock, CMD_COMPLETE, sizeof(CMD_COMPLETE), 0 );
        xferCount++;
    }
    std::cout << "MPD-RX exit.\n";
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
    init(index);
  
    // Enter daemon loop
    pthread_join(mpd_rx_id, NULL);
    pthread_join(mpd_tx_id, NULL);

    // Close system logs for the child process
    xclClose(uHandle);
    syslog(LOG_NOTICE, "Stopping daemon-name");
    closelog();
 
    // Terminate the child process when the daemon completes
    exit(EXIT_SUCCESS);
}

