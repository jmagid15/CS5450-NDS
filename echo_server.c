/******************************************************************************
* echo_server.c                                                               *
*                                                                             *
* Description: This file contains the C source code for an echo server.  The  *
*              server runs on a hard-coded port and simply write back anything*
*              sent to it by connected clients.  It does not support          *
*              concurrent clients.                                            *
*                                                                             *
* Authors: Athula Balachandran <abalacha@cs.cmu.edu>,                         *
*          Wolf Richter <wolf@cs.cmu.edu>                                     *
*                                                                             *
*******************************************************************************/

#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "parse.h"
#include <time.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/stat.h>
#include <arpa/inet.h>



#define ECHO_PORT 9999
#define BUF_SIZE 4096
#define ARGNUM 3

void handle_requests(int i, Request *request);
void doPOST(int i, Request *request);
void doHEAD(int i, Request *request);
void usage();
void getContentType(char *file_string, char *content_type);
void get_time(char *date);
FILE *open_log(const char *path);
void addToLog(char *logfilename, char *msg);
void print_log_time(FILE *fplog);


#define PORT "9999"   // port we're listening on

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


int main(int argc, char* argv[]) {

    char* logfilename = "log.txt";
    addToLog(logfilename, "Hello there");

    fd_set master;    // master file descriptor list
    fd_set read_fds;  // temp file descriptor list for select()
    int fdmax;        // maximum file descriptor number

    int listen_sock;     // listening socket descriptor
    int newfd;        // newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen;

    char buf[8192];    // buffer for client data
    int nbytes;

    char remoteIP[INET6_ADDRSTRLEN];

    int yes = 1;        // for setsockopt() SO_REUSEADDR, below
    int i, rv;

    struct addrinfo hints, *ai, *p;

//    char PORT;
//    sprintf(PORT, "%d", atoi(argv[1]));

//    char filepath;
//    filepath = argv[3];
//
//    char logfile;
//    logfile = argv[2];
//
//    if (argc != ARGNUM + 1) {
//        usage();
//    }

    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);

    // get us a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
        fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = ai; p != NULL; p = p->ai_next) {
        listen_sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listen_sock < 0) {
            continue;
        }

        // lose the pesky "address already in use" error message
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listen_sock, p->ai_addr, p->ai_addrlen) < 0) {
            close(listen_sock);
            continue;
        }

        break;
    }

    // if we got here, it means we didn't get bound
    if (p == NULL) {
        fprintf(stderr, "selectserver: failed to bind\n");
        exit(2);
    }

    freeaddrinfo(ai); // all done with this

    // listen
    if (listen(listen_sock, 10) == -1) {
        perror("listen");
        exit(3);
    }

    // add the listener to the master set
    FD_SET(listen_sock, &master);

    // keep track of the biggest file descriptor
    fdmax = listen_sock; // so far, it's this one

    // main loop
    for (;;) {
        read_fds = master; // copy it
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        // run through the existing connections looking for data to read
        for (i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) { // we got one!!
                if (i == listen_sock) {
                    // handle new connections
                    addrlen = sizeof remoteaddr;
                    newfd = accept(listen_sock,
                                   (struct sockaddr *) &remoteaddr,
                                   &addrlen);

                    if (newfd == -1) {
                        perror("accept");
                    } else {
                        FD_SET(newfd, &master); // add to master set
                        if (newfd > fdmax) {    // keep track of the max
                            fdmax = newfd;
                        }
                        printf("selectserver: new connection from %s on "
                                       "socket %d\n",
                               inet_ntop(remoteaddr.ss_family,
                                         get_in_addr((struct sockaddr *) &remoteaddr),
                                         remoteIP, INET6_ADDRSTRLEN),
                               newfd);
                    }
                } else {
                    // handle data from a client
                    if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
                        // got error or connection closed by client
                        if (nbytes == 0) {
                            // connection closed
                            printf("selectserver: socket %d hung up\n", i);
                        } else {
                            perror("recv");
                        }
                        close(i); // bye!
                        FD_CLR(i, &master); // remove from master set
                    } else {
//                        if (FD_ISSET(i, &master)) {
                            //fprintf(stdout, "Data received from socket %d. He said %s \n", i, buf);
                        Request *request = parse(buf, nbytes, i);
                        handle_requests(i, request);

//                            fprintf(stdout, "Request is %s \n", request->http_method);
                        if (send(i, buf, sizeof(buf), 0) != sizeof(buf)) {
                            perror("send error\n");
                        }
//                        }
                    }
                }
            }
        }
    }
    return EXIT_SUCCESS;
}

void handle_requests(int i, Request *request) {
    if (!strcasecmp(request->http_method, "GET")) {
        fprintf(stdout, "You (%d) said %s \n", i, request->http_method);
        //doGET(i, request);
        return;
    } else if (!strcasecmp(request->http_method, "HEAD")) {
        fprintf(stdout, "You (%d) said %s \n", i, request->http_method);
        doHEAD(i, request);
        return;
    } else if (!strcasecmp(request->http_method, "POST")) {
        fprintf(stdout, "You (%d) said %s \n", i, request->http_method);
        doPOST(i, request);
        return;
    } else {
        fprintf(stdout, "501 Not Implemented HTTP method is not implemented by the server\n");
    }
}

void doHEAD(int i, Request *request) {
    char respBuf[8192];
    char date[35];
    char content_type[1024];
    char *testStr;

    struct stat fileinfo;

    testStr = "www/big.html";
    stat(testStr, &fileinfo);

    fprintf(stdout, "Testing print: %lld\n", fileinfo.st_size);

    get_time(date);


    // Testing log printing
    //addToLog(fplog, "I hate this homework");


    // Get content type
    getContentType(request->http_uri, content_type);
    fprintf(stdout, "filetype is: %s\n", content_type);


    // Get content length



    sprintf(respBuf, "HTTP/1.1 200 OK\r\n");
    sprintf(respBuf + strlen(respBuf), "Server: Liso/1.0\r\n");
    sprintf(respBuf + strlen(respBuf), "Date: %s\r\n", date);
    sprintf(respBuf + strlen(respBuf), "Content-type: %s\r\n", content_type);
    sprintf(respBuf + strlen(respBuf), "Content-length: %lld\r\n", fileinfo.st_size);



    send(i, respBuf, strlen(respBuf), 0);


}

void getContentType(char *file_string, char *content_type) {
    if (strstr(file_string, ".html"))
        strcpy(content_type, "text/html");
    else if (strstr(file_string, ".css"))
        strcpy(content_type, "text/css");
    else if (strstr(file_string, ".js"))
        strcpy(content_type, "application/javascript");
    else if (strstr(file_string, ".jpg"))
        strcpy(content_type, "image/jpeg");
    else if (strstr(file_string, ".png"))
        strcpy(content_type, "image/png");
    else if (strstr(file_string, ".gif"))
        strcpy(content_type, "image/gif");
    else
        strcpy(content_type, "text/plain");
}


void doPOST(int i, Request *request) {

    char respBuf[8192];
    char date[35];

    get_time(date);

    sprintf(respBuf, "HTTP/1.1 200 No Content\r\n");
    sprintf(respBuf + strlen(respBuf), "Server: Liso/1.0\r\n");
    sprintf(respBuf + strlen(respBuf), "Date: %s\r\n", date);
    sprintf(respBuf + strlen(respBuf), "Content-type: text/html\r\n");
    sprintf(respBuf + strlen(respBuf), "Content-length: 0\r\n");
    send(i, respBuf, strlen(respBuf), 0);

//    fprintf(stdout, "We made it\r\n");
//    fprintf(stdout, "HTTP URI: %s\r\n", request->http_uri);
//    fprintf(stdout, "Buffer: \n%s\n", respBuf);
//
//    fprintf(fd,"Here is the int: %d\n", fd);
//    fprintf(stdout,"Here is the int: %d\n", temp);
}

FILE *open_log(const char *path) {

    FILE *logfile;
    logfile = fopen(path, "ab+");

    if (logfile == NULL) {
        fprintf(stdout, "Error occurred when trying to open log file.\n");
        exit(EXIT_FAILURE);
    }

    return logfile;
}

void print_log_time(FILE *fplog) {
    time_t t;
    struct tm *TimeCurr;

    t = time(NULL);
    TimeCurr = localtime(&t);

    fprintf(fplog, "[%04d-%02d-%02d %02d:%02d:%02d] ",
            TimeCurr->tm_year + 1900,
            TimeCurr->tm_mon + 1,
            TimeCurr->tm_mday,
            TimeCurr->tm_hour,
            TimeCurr->tm_min,
            TimeCurr->tm_sec
    );
}

void addToLog(char *logfilename, char *msg) {
    //print_log_time(fplog);
    FILE *fplog = fopen(logfilename, "a");

    fprintf(fplog, msg);
    fclose(fplog);
}


void get_time(char *date){
    struct tm tm;
    time_t now;
    now = time(0);
    tm = *gmtime(&now);
    strftime(date, 35, "%a, %d %b %Y %H:%M:%S GMT", &tm);
}

void usage(void) {
    fprintf(stderr, "usage: ./echo_server <HTTP port> <log file> <www folder>\n");
    exit(EXIT_FAILURE);
}
