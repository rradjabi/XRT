#include <stdio.h>	//printf
#include <string.h>	//strlen
#include <sys/socket.h>	//socket
#include <arpa/inet.h>	//inet_addr
#include <unistd.h>


#define MSG_SZ 128

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
	
	server.sin_addr.s_addr = inet_addr("127.0.0.1");
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

int socket_client_send( const int &sock, char *buffer )
{
    send( sock, buffer, MSG_SZ, 0 );
    return 0;
}
     
int socket_client_recv( const int &sock, char *buffer )
{
    int read_size = 0;
    if( (read_size = recv( sock, buffer, MSG_SZ, 0 )) > 0 )
        printf( "socket_*_recv(): %s\n", buffer );
    else
        printf( "socket_*_recv(): %i\n", read_size );

    return read_size;
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
 

