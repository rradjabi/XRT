#include <stdio.h>	//printf
#include <string.h>	//strlen
#include <sys/socket.h>	//socket
#include <arpa/inet.h>	//inet_addr
#include <unistd.h>
#include "socket.h"

int main(int argc , char *argv[])
{
    int sock;
    socket_client_init( sock );
	char message[MSG_SZ], server_reply[MSG_SZ];
    char CMD_TEST[] = "CMD_TEST\0";
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
	}
	
	close(sock);
	return 0;
}
