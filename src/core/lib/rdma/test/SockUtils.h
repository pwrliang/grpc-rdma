#ifndef _SOCKETUTILS_H
#define _SOCKETUTILS_H

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <strings.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>

class SocketUtils {
public:
    static inline int socket(int domain, int type, int protocol)
        {
            int sockfd = ::socket(domain, type, protocol);
            if(sockfd < 0){
                perror("Open socket failed!");
                exit(-1);
            }
            return sockfd;
        }

    static inline void setAddr(struct sockaddr_in &addr, const char *ip, int port)
        {
            int ret;
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);

            if( (ret = inet_pton(AF_INET, ip, &addr.sin_addr)) <= 0 ){
                if(ret == 0)
                    fprintf(stderr, "Incorrect address format: %s\n", ip);
                else
                    perror("Set address error!");
                exit(-1);
            }

        }

    static inline void hostnameToIp(char *hostname, char *ip)
        {
            struct hostent *he;
            struct in_addr **addr_list;

            if((he = gethostbyname(hostname)) == NULL){
                herror("gethostbyname");
                exit(-1);
            }

            addr_list = (struct in_addr **) he->h_addr_list;

            for(int i = 0; addr_list[i] != NULL; i++){
                strcpy(ip, inet_ntoa(*addr_list[i]));
                break;
            }
        }


    static inline void setAddr(struct sockaddr_in &addr, int port)
        {
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(port);
        }
};


#endif
