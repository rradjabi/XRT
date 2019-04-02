#include <stdio.h>  //printf
#include <string.h> //strlen
#include <sys/socket.h> //socket
#include <arpa/inet.h>  //inet_addr
#include <unistd.h>
#include <fstream>
////* from brian */
///#include <netdb.h>
///#include <stdio.h>
///#include <stdlib.h>
///#include <string.h>
///#include <sys/socket.h>
///#include <netinet/in.h>
///#include <arpa/inet.h>
///#include <sys/types.h>
///#include <sys/stat.h>
///#include <fcntl.h>
///#include <net/if.h>
///#include <linux/if_tun.h>
///#include <sys/ioctl.h>
#include <sys/wait.h>
///#include <unistd.h>


#define MSG_SZ 128

#define PORT 8838
#define IP_ADDR         "172.19.73.30" // host ip
#define SA struct sockaddr

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
        return -1;
    }

    return 0;
}

int send_args( int sock, struct drm_xocl_sw_mailbox *args )
{
    if( send( sock, (const void*)(args), sizeof(struct drm_xocl_sw_mailbox), 0 ) == -1 )
        exit(1);
    return 0;
}

int send_data( int sock, uint32_t *buf, int buflen )
{
    void *vbuf = reinterpret_cast<void*>(buf);
    unsigned char *pbuf = (unsigned char *)vbuf;

    while( buflen > 0 ) {
        int num = send( sock, pbuf, buflen, 0 );
        pbuf += num;
        buflen -= num;
    }

    return 0;
}

int recv_args( int sock, char* msg_buf, struct drm_xocl_sw_mailbox *args )
{
    int num = recv( sock, msg_buf, sizeof(struct drm_xocl_sw_mailbox), 0 );
    if( num < 0 )
        return num;
        
    memcpy( (void *)args, (void *)msg_buf, sizeof(struct drm_xocl_sw_mailbox) );
    std::cout  << "args->sz: " << args->sz << std::endl;
    return num;
}

int recv_data( int sock, uint32_t *pdata, int buflen )
{
    int num = 0;
    unsigned char *pbuf = reinterpret_cast<unsigned char*>(pdata);
    while( buflen > 0 ) {
        num = recv( sock, pbuf, buflen, 0 );
        
        if( num == 0 )
            break;
            
        if( num < 0 ) {
            std::cout << "Error, failed to recv: " << num << ", errno: " << errno << " errstr: " << strerror(errno) << std::endl;
            return num;
        }
        pbuf += num;
        buflen -= num;
        std::cout << "buflen=" << buflen << ", num=" << num << std::endl;
    }
    std::cout << "transfer complete: buflen=" << buflen << std::endl;
    return 0;
}

int socket_client_init( int &sock, const int port )
{
    struct sockaddr_in server;

    //Create socket
    sock = socket(AF_INET ,SOCK_STREAM , 0);
    if (sock == -1)
    {
        printf("Could not create socket");
    }
    puts("Socket created");

    server.sin_addr.s_addr = inet_addr(IP_ADDR);
    server.sin_family = AF_INET;
    server.sin_port = htons( port );

    //Connect to remote server
    if (connect(sock , (struct sockaddr *)&server , sizeof(server)) < 0)
    {
        perror("connect failed. Error");
        return 1;
    }

    puts("Connected\n");

    return 0;
}

// example code to setup communication channel between vm and host
// tcp is being used here as example.
// cloud vendor should implements this function
static void msd_comm_init(int *handle)//, int &sockfd, int &connfd, const int port)
{
    int sockfd, connfd, len;
//    int len;
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

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

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
    servaddr.sin_addr.s_addr = inet_addr(IP_ADDR);
    servaddr.sin_port = htons(PORT);

    // connect the client socket to server socket
    if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
        perror("connection with the server failed...");
        exit(1);
    }
    else
        printf("connected to the server..\n");

    *handle = sockfd;
}

/*
 * Stub code that finishs the communication channel.
 * Cloud vendor should implement this function.
 */
static void comm_fini(int handle)
{
    close(handle);
}

