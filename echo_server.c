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

#define ECHO_PORT 9999
#define BUF_SIZE 4096


int close_socket(int sock)
{
    if (close(sock)) {
        fprintf(stderr, "Failed closing socket.\n");
        return 1;
    }
    return 0;
}

int main(int argc, char* argv[])
{
    int sock;
    ssize_t readret;
    socklen_t cli_size;
    struct sockaddr_in addr, cli_addr;
    char buf[BUF_SIZE];

    fd_set master; // Set of all file descriptors
    fd_set read_fds; // Temporary file descriptor list for select
    int fdmax; // maximum file descriptor number
    int newfd;        // newly accept()ed socket descriptor for client
    int i;

    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);

    fprintf(stdout, "----- Echo Server -----\n");

    /* all networked programs must create a socket */
    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
    {
        fprintf(stderr, "Failed creating socket.\n");
        return EXIT_FAILURE;
    }

    // Bind socket
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ECHO_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    /* servers bind sockets to ports---notify the OS they accept connections */
    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)))
    {
        close_socket(sock);
        fprintf(stderr, "Failed binding socket.\n");
        return EXIT_FAILURE;
    }

    // file descriptors used to comm from program to outside devices. stdin, stdout, stderr = 0,1,2. socket 3 listens
    // first client connex = 4. files, disks, screen outputs, clients = same abstractions
    if (listen(sock, 5))
    {
        close_socket(sock);
        fprintf(stderr, "Error listening on socket.\n");
        return EXIT_FAILURE;
    }

    // add the listener socket to the master set
    FD_SET(sock, &master);

    // keep track of the largest file descriptor
    fdmax = sock; // so far, it's this one


    /* finally, loop waiting for input and then write it back */
    while (1) {

        // Select from possible file descriptors
        read_fds = master;
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select error\n");
            exit(4);
        }

        // OS accepts client and gives it a socket

        for (i=0; i <= fdmax; i++) {
            // Code inspired by Beej guide
            if (FD_ISSET(i, &read_fds)) { // Connection made
                if (i == sock) {
                    cli_size = sizeof(cli_addr);
                    newfd = accept(sock, (struct sockaddr *) &cli_addr, &cli_size);
                    if (newfd == -1) {
                        perror("accept issue\n");
                    } else {
                        FD_SET(newfd, &master); // add to master set
                        if (newfd > fdmax) {
                            fdmax = newfd;
                        }
                        fprintf(stdout, "New cxn in socket %d\n", newfd);
                    }
                } else {
                    // Data from client
                    readret = 0;
                    if ((readret = recv(i, buf, BUF_SIZE, 0)) <= 0) {
                        if (readret < 0) {
                            // Connection closed
                            perror("recv error\n");
                        }
                        close(i);
                        FD_CLR(i, &master);

                    } else {
                        // Received data from a client
                        if (FD_ISSET(i, &master)) {
                            fprintf(stdout, "Data received from socket %d \n",i);
                            if (send(i, buf, readret, 0) != readret) {
                                perror("send error\n");
                            }
                        }
                    }
                }
            }
        }
    }

    close_socket(sock);

    return EXIT_SUCCESS;
}
