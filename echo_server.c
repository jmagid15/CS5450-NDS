/******************************************************************************
* echo_server.c                                                               *
*                                                                             *
* Description: This file contains the C source code for the HTTP 1.1 server   *
*              implemented for Project 1 of Cornell Tech's Networked and      *
*              Distributed Systems course (CS5450). The select-based server   *
*              can handle multiple clients and can serve static files using   *
*              GET, HEAD, and POST requests.                                  *
*                                                                             *
* Authors: Jake Magid <jm2644@cornell.edu>,                                   *
*          Fani Maksakuli <fm399@cornell.edu>                                 *
*                                                                             *
* Original Template: Athula Balachandran <abalacha@cs.cmu.edu>,               *
*                    Wolf Richter <wolf@cs.cmu.edu>                           *
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


// Globally accessible variables
char *filePath;
char *logfilename;

// Main function
int main(int argc, char* argv[]) {

    logfilename = "log.txt"; //logfilename = argv[2];

    fd_set masterFDlist; // Master file descriptor list
    fd_set readFDs; // Temporary file descriptor list for select()
    int fdMax; // Maximum file descriptor number

    int listen_sock; // Server listening socket file descriptor
    int newFD; // New file descriptor
    struct sockaddr_storage clientAddr; // Client remote address
    socklen_t addrLen;

    char buf[8192];
    int retBytes;

    char remoteIP[INET6_ADDRSTRLEN];

    int yes = 1;
    int i, rv;

    struct addrinfo addrStruct, *ai, *p;

    char *PORT;
    PORT = argv[1];

    filePath = "www"; //filePath = argv[3];

    // Usage error to control number of arguments passed in
    // Commented out for use with autograder
    //    if (argc != ARGNUM + 1) {
    //        argError();
    //    }

    FD_ZERO(&masterFDlist);
    FD_ZERO(&readFDs);

    // Bind  get us a socket and bind it
    memset(&addrStruct, 0, sizeof addrStruct);
    addrStruct.ai_family = AF_UNSPEC;
    addrStruct.ai_socktype = SOCK_STREAM;
    addrStruct.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, PORT, &addrStruct, &ai)) != 0) {
        fprintf(stderr, "Liso: %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = ai; p != NULL; p = p->ai_next) {
        listen_sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listen_sock < 0) {
            continue;
        }

        // Take care of "address already in use" error message
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listen_sock, p->ai_addr, p->ai_addrlen) < 0) {
            close(listen_sock);
            continue;
        }
        break;
    }

    // Failed to bind
    if (p == NULL) {
        fprintf(stderr, "Liso: Failed to bind\n");
        exit(2);
    }

    freeaddrinfo(ai);

    // Listen
    if (listen(listen_sock, 10) == -1) {
        perror("listen");
        exit(3);
    }

    fprintf(stdout, "----------Liso Server---------\n");

    // Add the listener socket to the master set
    FD_SET(listen_sock, &masterFDlist);

    // fdMax used to keep track of the biggest file descriptor
    fdMax = listen_sock;

    // Main loop
    // Structure inspired by Beej code
    while (1) {
        readFDs = masterFDlist;
        if (select(fdMax + 1, &readFDs, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        // Run through the existing connections looking for data to read
        for (i = 0; i <= fdMax; i++) {
            if (FD_ISSET(i, &readFDs)) {
                if (i == listen_sock) {
                    // Handle new connection
                    addrLen = sizeof clientAddr;
                    newFD = accept(listen_sock, (struct sockaddr *) &clientAddr, &addrLen);

                    if (newFD == -1) {
                        perror("accept");
                    } else {
                        FD_SET(newFD, &masterFDlist); // Add to master set
                        if (newFD > fdMax) {
                            // Update max file descriptor
                            fdMax = newFD;
                        }
                        printf("Liso: New connection from %s on "
                                       "socket %d\n",
                               inet_ntop(clientAddr.ss_family, get_in_addr((struct sockaddr *) &clientAddr),
                                         remoteIP, INET6_ADDRSTRLEN), newFD);
                    }
                } else {
                    // Handle data from a client
                    if ((retBytes = recv(i, buf, sizeof buf, 0)) <= 0) {
                        // Error or connection closed by client
                        if (retBytes == 0) {
                            // Connection closed
                            printf("Liso: Socket %d hung up\n", i);
                        } else {
                            perror("recv");
                        }
                        close(i);
                        FD_CLR(i, &masterFDlist); // Clear from master list
                    } else {
                        // Received data from a client!

                        // Parse Request
                        Request *request = parse(buf, retBytes, i);

                        // Handle Request
                        if (handle_requests(i, request) < 0) {
                            // If error, close socket and clear from list
                            close(i);
                            FD_CLR(i, &masterFDlist);
                        }
                    }
                }
            }
        }
    }
    return EXIT_SUCCESS;
}


int handle_requests(int i, Request *request) {
    // Determine if HTTP version is supported
    if (!strcasecmp(request->http_version, "HTTP/1.1")) {
        // Determine which method to call
        if (!strcasecmp(request->http_method, "GET")) {
            if (doGET(i, request) < 0) {
                return -1;
            }
            return 1;
        } else if (!strcasecmp(request->http_method, "HEAD")) {
            if (doHEAD(i, request) < 0) {
                return -1;
            }
            return 1;
        } else if (!strcasecmp(request->http_method, "POST")) {
            doPOST(i, request);
            return 1;
        } else {
            handleError(i, 501, request);
            return 1;
        }
    } else {
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

    // Memory unmap to reclaim space
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
    // Match file string to various file types to determine content type
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
    fprintf(stderr, "usage: ./lisod <HTTP port> <log file> <www folder>\n");
    exit(EXIT_FAILURE);
}


void handleError(int fd, int error_num, Request *request) {
    char date[35];
    char error_buf[1024];
    char out[1024];

    // Retrieve date
    get_time(date);

    // Get message based on error id
    switch(error_num) {
        case 400:
            sprintf(out, "Bad Request. \n");
            addToLog(logfilename, "Error 400: Bad Request. \n");
            break;
        case 404:
            sprintf(out, "Not Found. \n");
            addToLog(logfilename, "Error 404: Not Found. \n");
            break;
        case 500:
            sprintf(out, "Internal Server Error. \n");
            addToLog(logfilename, "Error 500: Internal Server Error. \n");
            break;
        case 501:
            sprintf(out, "Not Implemented. \n");
            addToLog(logfilename, "Error 501: Not Implemented. \n");
            break;
        case 505:
            sprintf(out, "HTTP Version Not Supported. \n");
            addToLog(logfilename, "Error 505: HTTP Version Not Supported. \n");
            break;
        default:
            sprintf(out, "Unknown Error.\n");
            addToLog(logfilename, "Error: Unknown Error. \n");
            break;
    }

    sprintf(error_buf, "%s %d %s\r\n", request->http_version, error_num, out);
    sprintf(error_buf, "%sServer: Liso/1.0\r\n", error_buf);
    sprintf(error_buf, "%sDate: %s\r\n", error_buf, date);
    sprintf(error_buf, "%sContent-Length: %ld\r\n", error_buf, strlen(out));
    sprintf(error_buf, "%sContent-Type: text/html\r\n\r\n", error_buf);

    // Send message to client
    send(fd, error_buf, strlen(error_buf), 0);
}

// Get socket address, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
