/*  ---------------------------------------------------------------------------

    This program has been rewritten or modified by Altair Semiconductor, Ltd.

    (c) copyright 2014 Altair Semiconductor, Ltd.

   ------------------------------------------------------------------------- */

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <pthread.h>

#include <errno.h>
#include <pwd.h>

//#define DEVELOPMENT_ENVIRONMENT
#define AT_SOCKET_PROCESS_NAME "radio"
#define MAX_SIZE 65535



enum
{
    ALT_AT_SOCKET_RES_ERROR       = 0,
    ALT_AT_SOCKET_RES_OK          = 1,
    ALT_AT_SOCKET_RES_UNSOLICITED = 3
};
    
static int writeToATSocket(int sock_desc, const char *atCmd, const char* atCmdPrefix)
{

    static unsigned char buffer[MAX_SIZE];

    int buffer_size = 4 + strlen(atCmd) + 1 + (atCmdPrefix ? strlen(atCmdPrefix) +1 : 0);

    unsigned char l1 = (buffer_size)/ 256;
    unsigned char l2 = (unsigned char)(buffer_size);

    //printf("%s size %d %d %d\n",__FUNCTION__,buffer_size,l1,l2);

    buffer[0] = '\x03';
    buffer[1] = '\x00';
    buffer[2] = l1;
    buffer[3] = l2;
    memcpy(buffer + 4,atCmd,strlen(atCmd) +1);

    if (atCmdPrefix) 
    {
        memcpy((buffer +5 + strlen(atCmd)),atCmdPrefix,strlen(atCmdPrefix)+1);
    }


    if ( write(sock_desc, buffer, buffer_size) != buffer_size )
    {
        printf("failed to write buffer\n");
        return 0;
    }
  
    return 1;
}

static char* readFromAtSocket(int sock_desc, unsigned char *type ,char *buffer, size_t *buffer_size)
{
    unsigned char c,l1,l2;
    size_t rcv_b_size;

    if (read(sock_desc, &c, 1) > 0 && c == '\x03' &&
        read(sock_desc, &c, 1) > 0 && c == '\x00' &&
        read(sock_desc, &l1, 1) > 0 &&
        read(sock_desc, &l2, 1) > 0 && 
        read(sock_desc, type, 1) > 0 && 
        ((rcv_b_size = ((l1*256)+l2 - 5) ) <= *buffer_size) &&
        (size_t)read(sock_desc, buffer, rcv_b_size) == rcv_b_size)
    {
        *buffer_size = rcv_b_size;
         return buffer;
    }
    
    return NULL;
}

static int connect_socket(const char* socket_name, int isAbstract)
{
    struct sockaddr_un client_addr;
    socklen_t alen;
    size_t namelen = strlen(socket_name);
    //int n = 1;

    int sock_descriptor = socket(AF_LOCAL, SOCK_STREAM, 0);

    if (sock_descriptor < 0)
        return -1;

    bzero((char *)&client_addr, sizeof(client_addr));

    client_addr.sun_family = AF_LOCAL;

    //printf("%s name %s \n",__FUNCTION__,socket_name);

    client_addr.sun_path[0] = '\0';

    alen = namelen + offsetof(struct sockaddr_un, sun_path);

    if (isAbstract) 
    {
        memcpy(client_addr.sun_path + 1, socket_name, namelen);
        alen = alen + 1;
    }
    else
    {
        memcpy(client_addr.sun_path, socket_name, namelen);
    }

    //setsockopt(sock_descriptor, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));

    if (connect(sock_descriptor, (struct sockaddr *)&client_addr, alen) < 0)
    {
        //printf("%s Failed to connect to %d\n",__FUNCTION__,errno);
        close(sock_descriptor);
        return -1;
    }

    return sock_descriptor;
}

int main(int argc, const char* argv[])
{
    int sock_descriptor;

    int result = 1;
    char resultBuff[MAX_SIZE];
    unsigned char type;
    size_t buffer_size = sizeof(resultBuff);

    const char* init_rc_socket_name = "/dev/socket/altair_at_socket";
    const char* dev_socket_name = "altair_at_socket";


    if ((argc == 1) || (argc > 3))
    {
        printf("invalid num of arguments\nplease use %s \"at command\" [\"response prefix\"]\n",argv[0]);
        return 1;
    }

    if ((sock_descriptor = connect_socket(init_rc_socket_name,0)) < 0)
    {
        #ifdef DEVELOPMENT_ENVIRONMENT
        if ((sock_descriptor = connect_socket(dev_socket_name,1)) < 0)
        #endif
        {
            printf("%s Failed to create socket %d\n",__FUNCTION__,errno);
            return 1;
        }
        #ifdef DEVELOPMENT_ENVIRONMENT
        else
        {
            printf("connected to @%s\n",dev_socket_name);
        }
        #endif
    }
    else
    {
        printf("connected to %s\n",init_rc_socket_name);
    }

    printf("cmd: %s | ",argv[1]);
    if (argc > 2) 
    {
        printf("prefix: %s\n",argv[2]);
    }
    else
    {
        printf("no prefix\n");
    }

    if (!writeToATSocket(sock_descriptor,argv[1],(argc > 2)? argv[2] : NULL))
    {
        printf("Failed to write to at socket \n");
        goto error;
    }
    
    if (!readFromAtSocket(sock_descriptor,&type,resultBuff,&buffer_size)) 
    {
        printf("failed to read from at socket \n");
        goto error;
    }

    printf("\n");
    printf(resultBuff);
    printf("\n\n");
    if (type == ALT_AT_SOCKET_RES_OK) 
    {
        printf("at command successful \n");
    }
    else if (type == ALT_AT_SOCKET_RES_ERROR) 
    {
        printf("at command failed \n");
    }
    else if (type == ALT_AT_SOCKET_RES_UNSOLICITED) 
    {
        printf("unsolicited at command \n");
    }

    result = 0;
error:
    close(sock_descriptor);
    return result;
}
