#include <stdio.h>
#include <string.h>	//strlen
#include <sys/socket.h>
#include <arpa/inet.h>	//inet_addr
#include <unistd.h>	//write
#include "socket.h"

int main(int argc , char *argv[])
{
    int sock, client_sock, read_size;
	char client_message[MSG_SZ];
    socket_server_init( sock, client_sock );
    char CMD_RESPONSE[] = "CMD_RESPONSE\0";
	
	//Receive a message from client
	while( (read_size = recv(client_sock , client_message , MSG_SZ, 0)) > 0 )
	{
        printf("recv: %s\n", client_message);

        send( client_sock, CMD_RESPONSE, sizeof(CMD_RESPONSE), 0 );
	}
	
	if(read_size == 0)
	{
		puts("Client disconnected");
		fflush(stdout);
	}
	else if(read_size == -1)
	{
		perror("recv failed");
	}
	
	return 0;
}

