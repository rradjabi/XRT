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
pthread_t msd_tx_id;
pthread_t msd_rx_id;
const std::string PemFile = "/home/rradjabi/keys/localhost/localhost.pem";

void *msd_tx(void *handle_ptr)
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
    socket_server_init( sock, client_sock, PORT_MSD_TO_MPD );

    std::cout << "               [XCLMGMT->XOCL Intercept ON (HAL)]\n";
    for( ;; ) {
        std::cout << "			[MSD-TX]:" << xferCount << " (1) MSD TX IOCTL \n";
        args.sz = prev_sz;
        ret = xclMSD(handle, &args);
        std::cout << "			[MSD-TX]:" << xferCount << " (1.1) return from xclMSD\n";
        if( ret != 0 ) {
            if( errno != EMSGSIZE ) {
                std::cout << "              MSD: transfer failed for other reason\n";
                exit(1);
            }
            
            if( resize_buffer( args.data, args.sz ) != 0 ) {
                std::cout << "              MSD: resize_buffer() failed...exiting\n";
                exit(1);
            }

            prev_sz = args.sz; // store the newly alloc'd size
            ret = xclMSD(handle, &args);

            if( ret != 0 ) {
                std::cout << "              MSD: second transfer failed, exiting.\n";
                exit(1);
            }
        }
        
        std::cout << "			[MSD-TX]:" << xferCount << " (2) send args over socket \n";
        send_args( client_sock, &args );
        std::cout << "			[MSD-TX]:" << xferCount << " (3) send data over socket \n";
        send_data( client_sock, args.data, args.sz );
        std::cout << "                [MSD-TX]: " << xferCount << " complete.\n";
        xferCount++;
    }

    std::cout << "          MSD-TX exit.\n";
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

    int sock, client_sock, read_size;
    char client_message[MSG_SZ];
    socket_server_init( sock, client_sock, PORT_MPD_TO_MSD );

    for( ;; ) {
        std::cout << "			[MSD-RX]:" << xferCount << " (1) recv_args\n";        
        recv_args( client_sock, client_message, &args );
        args.data = pdata; // must do this after recv_args
        args.is_tx = false; // must do this
        
        std::cout << "			[MSD-RX]:" << xferCount << " (2) resize buffer\n";
        if( args.sz > prev_sz ) {
            std::cout << "args.sz(" << args.sz << ") > prev_sz(" << prev_sz << ") \n";
            resize_buffer( args.data, args.sz );
            prev_sz = args.sz;
        } else {
            std::cout << "don't need to resize buffer\n";
        }

        std::cout << "			[MSD-RX]:" << xferCount << " (3) recv_data\n";        
        if( recv_data( client_sock, args.data, args.sz ) != 0 ) {
            std::cout << "bad retval from recv_data(), exiting.\n";
            exit(1);
        }
                
        std::cout << "			[MSD-RX]:" << xferCount << " (4) xclMSD \n";
        ret = xclMPD(handle, &args);
        if( ret != 0 ) {
            std::cout << "          MSD-RX: transfer error: " << strerror(errno) << std::endl;
            exit(1);
        }
        std::cout << "          [MSD-RX]: " << xferCount << " complete.\n";
        
        xferCount++;
    }
    std::cout << "          MSD-RX exit.\n";
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
 
    //~ // Define variables
    //~ pid_t pid, sid;
 
    //~ // Fork the current process
    //~ pid = fork();
    //~ // The parent process continues with a process ID greater than 0
    //~ if(pid > 0)
    //~ {
        //~ exit(EXIT_SUCCESS);
    //~ }
    //~ // A process ID lower than 0 indicates a failure in either process
    //~ else if(pid < 0)
    //~ {
        //~ exit(EXIT_FAILURE);
    //~ }
    //~ // The parent process has now terminated, and the forked child process will continue
    //~ // (the pid of the child process was 0)
 
    //~ // Since the child process is a daemon, the umask needs to be set so files and logs can be written
    //~ umask(0);
 
    //~ // Open system logs for the child process
    //~ openlog("daemon-named", LOG_NOWAIT | LOG_PID, LOG_USER);
    //~ syslog(LOG_NOTICE, "Successfully started daemon-name");
 
    //~ // Generate a session ID for the child process
    //~ sid = setsid();
    //~ // Ensure a valid SID for the child process
    //~ if(sid < 0)
    //~ {
        //~ // Log failure and exit
        //~ syslog(LOG_ERR, "Could not generate session ID for child process");
 
        //~ // If a new session ID could not be generated, we must terminate the child process
        //~ // or it will be orphaned
        //~ exit(EXIT_FAILURE);
    //~ }
 
    //~ // Change the current working directory to a directory guaranteed to exist
    //~ if((chdir("/")) < 0)
    //~ {
        //~ // Log failure and exit
        //~ syslog(LOG_ERR, "Could not change working directory to /");
 
        //~ // If our guaranteed directory does not exist, terminate the child process to ensure
        //~ // the daemon has not been hijacked
        //~ exit(EXIT_FAILURE);
    //~ }
 
    // A daemon cannot use the terminal, so close standard file descriptors for security reasons
//    close(STDIN_FILENO);
//    close(STDOUT_FILENO);
//    close(STDERR_FILENO);
 
    // Daemon-specific intialization should go here
    const int SLEEP_INTERVAL = 5;

    init(index);
  
    // Enter daemon loop
    pthread_join(msd_tx_id, NULL);
    pthread_join(msd_rx_id, NULL);

    // Close system logs for the child process
    xclClose(uHandle);
    syslog(LOG_NOTICE, "Stopping daemon-name");
    closelog();
 
    // Terminate the child process when the daemon completes
    exit(EXIT_SUCCESS);
}
