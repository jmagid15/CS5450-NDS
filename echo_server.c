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
#include <sys/mman.h>
#include <fcntl.h>


//#define ECHO_PORT 9999
#define BUF_SIZE 4096
#define ARGNUM 3

// Headers
int handle_requests(int i, Request *request);
void doPOST(int i, Request *request);
int doHEAD(int i, Request *request);
int doGET(int i, Request *request);
void argError();
void getContentType(char *file_string, char *content_type);
void get_time(char *date);
void addToLog(char *logfilename, char *msg);
void handleError(int fd, int error_num, Request *request);
void *get_in_addr(struct sockaddr *sa);

//#define PORT "9999"   // port we're listening on

// Globally accessible variables
char *filePath;
char *logfilename;

// Main function
int main(int argc, char* argv[]) {

    logfilename = "log.txt";
    //addToLog(logfilename, "Hello there!\n");

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

    char *PORT;
    PORT = argv[1];

    filePath = "www"; //argv[3];

//    char logfile;
//    logfile = argv[2];

//    if (argc != ARGNUM + 1) {
//        argError();
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

    fprintf(stdout, "----------Liso Server---------\n");

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
                    newfd = accept(listen_sock, (struct sockaddr *) &remoteaddr, &addrlen);

                    if (newfd == -1) {
                        perror("accept");
                    } else {
                        FD_SET(newfd, &master); // add to master set
                        if (newfd > fdmax) {    // keep track of the max
                            fdmax = newfd;
                        }
                        printf("selectserver: new connection from %s on "
                                       "socket %d\n",
                               inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr *) &remoteaddr),
                                         remoteIP, INET6_ADDRSTRLEN), newfd);
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

                        // Parse Request
                        Request *request = parse(buf, nbytes, i);

                        // Handle Request
                        if (handle_requests(i, request) < 0) {
                            close(i);
                            FD_CLR(i, &master);
                        }

//                        }
                    }
                }
            }
        }
    }
    return EXIT_SUCCESS;
}


int handle_requests(int i, Request *request) {
    if (!strcasecmp(request->http_version, "HTTP/1.1")) {
        if (!strcasecmp(request->http_method, "GET")) {
            fprintf(stdout, "You (%d) said %s \n", i, request->http_method);
            if (doGET(i, request) < 0) {
                return -1;
            }
            return 1;
        } else if (!strcasecmp(request->http_method, "HEAD")) {
            fprintf(stdout, "You (%d) said %s \n", i, request->http_method);
            if (doHEAD(i, request) < 0) {
                return -1;
            }
            return 1;
        } else if (!strcasecmp(request->http_method, "POST")) {
            fprintf(stdout, "You (%d) said %s \n", i, request->http_method);
            doPOST(i, request);
            return 1;
        } else {
            fprintf(stdout, "501 Not Implemented HTTP method is not implemented by the server\n");
            handleError(i, 501, request);
            return 1;
        }
    } else {
        fprintf(stdout, "505 HTTP version is not supported by the server\n");
        handleError(i, 505, request);
        return 1;
    }
}


int doGET(int i, Request *request) {
    struct stat fileinfo;
    char fullPath[256];
    char *filePtr;
    int fd, filesize;


    // Decide which path to look at
    if (!strcmp(request->http_uri, "/")) {
        sprintf(fullPath, "%s%s", filePath, "/index.html");
    } else {
        sprintf(fullPath, "%s%s", filePath, request->http_uri);
    }

    // Try to open the file. Print error and add to log if fail.
    fd = open(fullPath, O_RDONLY, 0);
    if (fd < 0) {
        handleError(i, 404, request);
        addToLog(logfilename, "Error: Can't open file \n");
        return -1;
    }

    // Get file info (content-length, last modified)
    stat(fullPath, &fileinfo);

    // Set filesize
    filesize = fileinfo.st_size;

    // Send headers
    if (doHEAD(i, request) < 0) {
        return -1;
    }

    // Create pointer of file contents using memory map
    filePtr = mmap(0, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    // Send to client
    send(i, filePtr, filesize, 0);

    // Memory unmap to reclaim space.
    munmap(filePtr, filesize);

    return 1;
}


int doHEAD(int i, Request *request) {
    char respBuf[8192];
    char date[35];
    char content_type[1024];
    char fullPath[256];
    struct stat fileinfo;


    // Decide which path to look at
    if (!strcmp(request->http_uri, "/")) {
        sprintf(fullPath, "%s%s", filePath, "/index.html");
    } else {
        sprintf(fullPath, "%s%s", filePath, request->http_uri);
    }

    // Check if file exists
    int fd;
    fd = open(fullPath, O_RDONLY, 0);
    if (fd < 0) {
        handleError(i, 404, request);
        addToLog(logfilename, "Error: Can't open file \n");
        return -1;
    }

    // Get file info (content-length, last modified)
    stat(fullPath, &fileinfo);

    // Get date
    get_time(date);


    // Get content type
    getContentType(request->http_uri, content_type);


    // Get last modified
    char lastModTime[512];
    struct tm temp;
    temp = *gmtime(&fileinfo.st_mtime);
    strftime(lastModTime, 64, "%a, %d %b %Y %H:%M:%S GMT", &temp);


    // Print to buffer and send to file descriptor

    sprintf(respBuf, "HTTP/1.1 200 OK\r\n");
    sprintf(respBuf + strlen(respBuf), "Server: Liso/1.0\r\n");
    sprintf(respBuf + strlen(respBuf), "Date: %s\r\n", date);
    sprintf(respBuf + strlen(respBuf), "Connection: keep-alive\r\n");
    sprintf(respBuf + strlen(respBuf), "Content-type: %s\r\n", content_type);
    sprintf(respBuf + strlen(respBuf), "Content-length: %lld\r\n", fileinfo.st_size);
    sprintf(respBuf + strlen(respBuf), "Last-modified: %s\r\n\r\n", lastModTime);

    send(i, respBuf, strlen(respBuf), 0);

    close(fd);

    return 1;
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

}


void addToLog(char *logfilename, char *msg) {
    FILE *fplog = fopen(logfilename, "a");

    time_t t = time(NULL);
    struct tm *TimeCurr = localtime(&t);

    fprintf(fplog, "[%04d-%02d-%02d %02d:%02d:%02d] ",
            TimeCurr->tm_year + 1900,
            TimeCurr->tm_mon + 1,
            TimeCurr->tm_mday,
            TimeCurr->tm_hour,
            TimeCurr->tm_min,
            TimeCurr->tm_sec
    );

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


void argError(void) {
    fprintf(stderr, "usage: ./echo_server <HTTP port> <log file> <www folder>\n");
    exit(EXIT_FAILURE);
}


void handleError(int fd, int error_num, Request *request) {
    // retrieve date initialize the buffers.
    char date[35];
    char error_buf[1024];
    char out[1024];
    get_time(date);

    // Get message based on error id
    switch(error_num) {
        case 400:
            sprintf(out, "Bad Request. \n");
            break;
        case 404:
            sprintf(out, "Not Found. \n");
            break;
        case 500:
            sprintf(out, "Internal Server Error. \n");
            break;
        case 501:
            sprintf(out, "Do you see this: Not Implemented. \n");
            break;
        case 505:
            sprintf(out, "HTTP Version Not Supported. \n");
            break;
        default:
            sprintf(out, "Unknown Error.\n");
            break;
    }

    sprintf(error_buf, "%s %d %s\r\n", request->http_version, error_num, out);
    sprintf(error_buf, "%sServer: Liso/1.0\r\n", error_buf);
    sprintf(error_buf, "%sDate: %s\r\n", error_buf, date);
    sprintf(error_buf, "%sContent-Length: %ld\r\n", error_buf, strlen(out));
    sprintf(error_buf, "%sContent-Type: text/html\r\n\r\n", error_buf);
    send(fd, error_buf, strlen(error_buf), 0);
    fprintf(stdout, "ERROR sent to the Client!\n");
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
