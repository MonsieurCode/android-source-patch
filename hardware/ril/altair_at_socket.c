/* //device/system/altair-ril/altair_at_socket.c
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/
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
#include <cutils/sockets.h>

#include <errno.h>
#include <pwd.h>
#include <atchannel.h>

#define LOG_TAG "RIL_ALT_AT_SOCKET"
#include <utils/Log.h>
#ifdef 	RIL_EMULATE_4_2
#define RLOGD	ALOGD
#define RLOGW	ALOGW
#define RLOGE	ALOGE
#define RLOGI	ALOGI
#endif

//#define DEVELOPMENT_ENVIRONMENT
#define AT_SOCKET_PROCESS_NAME "radio"
#define MAX_SIZE 65535

extern void *onUnsolicitedAtSocketUserdata;

typedef struct
{
    int conn_desc;
    pthread_mutex_t writeToAtSocket_mutex;
} altairRilATSocketData;

enum
{
    ALT_AT_SOCKET_RES_ERROR       = 0,
    ALT_AT_SOCKET_RES_OK          = 1,
    ALT_AT_SOCKET_RES_UNSOLICITED = 3
};
    
static char *readFromATSocket(int sock_desc, char *buffer, size_t *buffer_size)
{
    unsigned char c, l1, l2;
    
    if ( read(sock_desc, &c, 1) > 0 && c == '\x03' &&
         read(sock_desc, &c, 1) > 0 && c == '\x00' &&
         read(sock_desc, &l1, 1) > 0 &&
         read(sock_desc, &l2, 1) > 0 && ((*buffer_size = ((l1*256) + l2 - 4)) >= 4U) &&
         (size_t)read(sock_desc, buffer, *buffer_size) == (*buffer_size))
        return buffer;
    return NULL;
}

static int writeToAtSocket(int sock_desc, int type ,const char *buffer, size_t buffer_size, pthread_mutex_t *mutex)
{
    unsigned char prefix[5]= {'\x03',
                              '\x00',
                              ((unsigned char)((buffer_size + 5)/ 256)),
                              (unsigned char)(buffer_size+5),
                              (unsigned char)type};
    int result = 0;
    
    //RLOGD("%s goint to send prefix len 5\n",__FUNCTION__);

    pthread_mutex_lock(mutex);
    if (send(sock_desc, prefix, 5, MSG_NOSIGNAL) == 5)
    {
        //RLOGD("%s goint to send buffer len %d\n",__FUNCTION__,buffer_size);
        if ((size_t)send(sock_desc, buffer, buffer_size, MSG_NOSIGNAL) == buffer_size)
        {
            result = 1;
        }
    }
    pthread_mutex_unlock(mutex);
    return result;
}

static const char *processMessage(const char *message, const char *prefix, int* type, char *buff, size_t buffSize)
{
    ATResponse *p_response;
    ATLine *p_cur;
    int err;
    size_t len = 0;

    *type = ALT_AT_SOCKET_RES_ERROR;

    RLOGD("%s processMessage prefix %s | %s\n",__FUNCTION__,prefix,message);
    do
    {
        if (prefix) 
        {
            err = at_send_command_multiline (message, prefix, &p_response);
        }
        else
        {
            err = at_send_command (message, &p_response);
        }
        
    } while (err == AT_ERROR_COMMAND_PENDING);
    
    if (err == 0)
    {
        
        *type = (p_response->success != 0)?ALT_AT_SOCKET_RES_OK:ALT_AT_SOCKET_RES_ERROR;
    
        for (p_cur = p_response->p_intermediates; p_cur != NULL;p_cur = p_cur->p_next)
        {
        
            char *line = p_cur->line;
            size_t line_len = strlen(line);
            RLOGD("%s response: (%s)\n",__FUNCTION__, line);
            if (len + line_len + 3 <= buffSize)
            {
                memcpy(buff + len, line, line_len);
                len += line_len;
                memcpy(buff + len, "\n\r", 2);
                len+=2;
            }
        }
        
        strcpy(buff + len, p_response->finalResponse);
        len += strlen(p_response->finalResponse);
        
        buff[len] = '\0';
        at_response_free(p_response);
    }
    
    return buff;
}

void altairAtSocketOnUnsolicited(const char *s, void *userdata)
{
    altairRilATSocketData *atSocketData = (altairRilATSocketData *)userdata;
    RLOGD("%s unsolicited response:(%s)\n",__FUNCTION__, s);
    writeToAtSocket(atSocketData->conn_desc, ALT_AT_SOCKET_RES_UNSOLICITED, s, strlen(s)+1, &atSocketData->writeToAtSocket_mutex);
}

#ifdef DEVELOPMENT_ENVIRONMENT
static int development_socket()
{
    struct sockaddr_un serv_addr;
    const char* socket_name = "altair_at_socket";
    size_t namelen = strlen(socket_name);
    socklen_t alen;
    int n = 1;
            
    int sock_descriptor = socket(AF_LOCAL, SOCK_STREAM, 0);

    if (sock_descriptor < 0)
        return -1;

    bzero((char *)&serv_addr, sizeof(serv_addr));

    serv_addr.sun_family = AF_LOCAL;

    serv_addr.sun_path[0] = 0;
    memcpy(serv_addr.sun_path + 1, socket_name, namelen);
    alen = namelen + offsetof(struct sockaddr_un, sun_path) + 1;

    setsockopt(sock_descriptor, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));

    if (bind(sock_descriptor, (struct sockaddr *)&serv_addr, alen) < 0)
    {
        RLOGE("%s Failed to bind %d\n",__FUNCTION__,errno);
    }

    return sock_descriptor;
}
#endif //DEVELOPMENT_ENVIRONMENT

#ifndef DEVELOPMENT_ENVIRONMENT
static int checkSocketCred(int socket)
{
    struct ucred creds;
    socklen_t szCreds = sizeof(creds);
    struct passwd *pwd = NULL;
    int ret = 0;
    
    int err = getsockopt(socket, SOL_SOCKET, SO_PEERCRED, &creds, &szCreds);
    if (err == 0 && szCreds > 0)
    {
        errno = 0;
        pwd = getpwuid(creds.uid);
        if (pwd != NULL) {
            RLOGD("%s The ID of the socket process is %s\n",__FUNCTION__, pwd->pw_name);
            if (strcmp(pwd->pw_name, AT_SOCKET_PROCESS_NAME))
            {
                RLOGE("Altair at socket cannot accept connection from %s\n",pwd->pw_name);
            }
            else
            {
                ret = 1;
            }
        } else {
            RLOGE("%s Error on getpwuid() errno: %d",__FUNCTION__, errno);
        }
    } else {
        RLOGD("%s Error on getsockopt() errno: %d",__FUNCTION__, errno);
    }

    return ret;
}
#endif // ndef DEVELOPMENT_ENVIRONMENT

static void * atSocketMainLoop(void *param)
{
    int sock_descriptor;
    struct sockaddr_un serv_addr, client_addr;
    altairRilATSocketData atSocketData = {-1, PTHREAD_MUTEX_INITIALIZER};

    int result;
    char buff[MAX_SIZE];
    char resultBuff[MAX_SIZE];

    const char* init_rc_socket_name = "altair_at_socket";
    int size = sizeof(client_addr);

    if ((sock_descriptor = android_get_control_socket(init_rc_socket_name)) < 0)
    {
        #ifdef DEVELOPMENT_ENVIRONMENT
        if ((sock_descriptor = development_socket()) < 0)
        #endif
        {
            RLOGE("%s Failed to create socket\n",__FUNCTION__);
            return 0;
        }
    }

    while(1)
    {
        if (listen(sock_descriptor, 5) < 0)
        {
             RLOGE("%s Failed to listen on socket errno %d\n",__FUNCTION__,errno);
             break;
        }

        RLOGD("%s Waiting for connection...\n",__FUNCTION__);

        atSocketData.conn_desc = accept(sock_descriptor, (struct sockaddr *)&client_addr, &size);         
        if (atSocketData.conn_desc == -1)
        {
            RLOGE("%s Failed accepting connection errno %d\n",__FUNCTION__,errno);
            break;
        }
        else
        {
            RLOGD("%s Connected\n",__FUNCTION__);
        }

            
#ifndef DEVELOPMENT_ENVIRONMENT
        if (checkSocketCred(atSocketData.conn_desc)) 
#endif
        {
            onUnsolicitedAtSocketUserdata = &atSocketData;
        
            while(1)
            {
                size_t len = sizeof(buff);
                
                if (readFromATSocket(atSocketData.conn_desc, buff, &len))
                {
                    char *message = buff;
                    // the prefix is a null terminated string after the AT command null terminated string.
                    // if there is no prefix sent at all - then it means the AT command doesn't expect any answer.
                    char *prefix = (len > strlen(buff)+1)? buff+strlen(buff)+1 : NULL;
                    int type;
                    const char *result;
                    RLOGD("%s Received %s ,%s \n",__FUNCTION__, message,prefix);
                    result = processMessage(message, prefix, &type, resultBuff, sizeof(resultBuff));
                    writeToAtSocket(atSocketData.conn_desc, type, result, strlen(result)+1, &atSocketData.writeToAtSocket_mutex);
                }
                else
                {
                    RLOGE("%s Connection closed\n",__FUNCTION__);
                    break;
                }
            }

            onUnsolicitedAtSocketUserdata = NULL;
        }
        
        // Program should always close all sockets (the connected one as well as the listening one)
        // as soon as it is done processing with it
        shutdown(atSocketData.conn_desc,SHUT_RDWR);
        close(atSocketData.conn_desc);

        atSocketData.conn_desc = 0;
        
    }

    shutdown(sock_descriptor,SHUT_RDWR);
    close(sock_descriptor);
    return 0;
}

int altairAtSocketCreate()
{
    pthread_t s_tid_mainloop;
    pthread_attr_t attr;
    
    pthread_attr_init (&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    return pthread_create(&s_tid_mainloop, &attr, atSocketMainLoop, NULL);
}
