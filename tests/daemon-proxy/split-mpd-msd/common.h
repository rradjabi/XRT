#include <stdio.h>  //printf
#include <string.h> //strlen
#include <sys/socket.h> //socket
#include <arpa/inet.h>  //inet_addr
#include <unistd.h>
#include <fstream>

#define MSG_SZ 128

#define PORT_MPD_TO_MSD 8888
#define PORT_MSD_TO_MPD 8889
#define IP_ADDR         "172.19.73.30" // host ip

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
    recv( sock, msg_buf, sizeof(struct drm_xocl_sw_mailbox), 0 );
    memcpy( (void *)args, (void *)msg_buf, sizeof(struct drm_xocl_sw_mailbox) );
    std::cout  << "args->sz: " << args->sz << std::endl;
    return 0;
}

int recv_data( int sock, uint32_t *pdata, int buflen )
{
    unsigned char *pbuf = reinterpret_cast<unsigned char*>(pdata);
    while( buflen > 0 ) {
        int num = recv( sock, pbuf, buflen, 0 );
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
    sock = socket(AF_INET , SOCK_STREAM , 0);
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

int socket_server_init( int &sock, int &client_sock, const int port )
{
    struct sockaddr_in server, client;
    //Create socket
    sock = socket(AF_INET , SOCK_STREAM , 0);
    if (sock == -1)
    {
        printf("Could not create socket");
    }
    puts("Socket created");

    //Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons( port );

    //Bind
    if( bind(sock,(struct sockaddr *)&server , sizeof(server)) < 0)
    {
        //print the error message
        perror("bind failed. Error");
        return 1;
    }
    puts("bind done");

    //Listen
    listen(sock, 3);

    //Accept and incoming connection
    puts("Waiting for incoming connections...");
    int c = sizeof(struct sockaddr_in);

    //accept connection from an incoming client
    client_sock = accept(sock, (struct sockaddr *)&client, (socklen_t*)&c);
    if (client_sock < 0)
    {
        perror("accept failed");
        return 1;
    }
    puts("Connection accepted");

    return 0;
}
