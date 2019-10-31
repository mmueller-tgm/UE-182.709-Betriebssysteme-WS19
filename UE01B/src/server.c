/**
 * @file client.c
 * @author Maximilian Müller
 * @date 28.10.2019
 *
 * @brief A simple HTTP client, able to send http requests and displays the respnes in a file
 *
 * Some more detailed Comments
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include "server.h"

#define MAX_CONNECTIONS 10
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"

volatile sig_atomic_t quit = 0;

void handle_soft_exit(int signal) {
    quit = 1;
}

/**
 * Prints the correct usage of the Programm
 */

static void usage(char* pname) {
	fprintf(stderr, "Usage: %s [-p PORT] [-i INDEX] DOC_ROOT\n"
    "\t-p PORT to bind to (default = 8080)\n"
    "\t-i filename of file, which is transmitted when a folder is requested\n", pname);
	exit(EXIT_FAILURE);
}


int handle_connection(FILE* sockfile, char* documentroot, char* indexfile) {
    char *filepath = malloc(sizeof documentroot);
    char *buf = NULL;
    size_t len = 0;

    if (!getline(&buf, &len, sockfile)) return EXIT_FAILURE;
    printf("%s", buf);
    
    char* method = strtok(buf, " ");
    char* path = strtok(NULL, " ");
    char* protocol = strtok(NULL, "\r");

    if (!method||!path||!protocol) {
        printf("Bad Request\n");
        send_error(sockfile, 400, "Bad Request", "Bad Request");
        return EXIT_SUCCESS;
    }
    
    if (!strcmp(method, "GET")) {
        printf("Not Implemented\n");
        send_error(sockfile, 501, "Not Implemented", "Not Implemented");
        return EXIT_SUCCESS;
    }

    strcpy(filepath, documentroot);
    strcat(filepath, path);
    if (filepath[strlen(filepath)-1]=='/')
        strcat(filepath, indexfile);
    
    printf("Parsed request\n");
    printf("Method: %s; File: %s; Protocol: %s\n", method, filepath, protocol);

    
    while (strlen(buf)>2) {
        getline(&buf, &len, sockfile);
    }
    fseek(sockfile, 0, SEEK_CUR);

    send_response(sockfile, filepath);
    return EXIT_SUCCESS;
}

int http_server(char* port, char* documentroot, char* indexfile) {
    struct addrinfo hints, *ai;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;


    int res = getaddrinfo(NULL, port, &hints, &ai);
    if (res!=0) {
        fprintf(stderr, "Failed to get addrinfo!:\n%s", gai_strerror(res));
        return(EXIT_FAILURE);
    }

    int sockfd = -1;
    errno = EINTR;
    while ((sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) <0) {
        if (errno != EINTR) {
            fprintf(stderr, "Failed to create socket! \n%s\n", strerror(errno));
            return(EXIT_FAILURE);
        }
    }

    while (bind(sockfd, ai->ai_addr, ai->ai_addrlen) < 0) {
        if (errno != EINTR) {
            fprintf(stderr, "Failed to bind socket! \n%s\n", strerror(errno));
            return(EXIT_FAILURE);
        }
    }

    int optval = 2;
    while (setsockopt(sockfd, SOL_SOCKET, (SO_REUSEADDR|SO_REUSEPORT), &optval, sizeof optval)<0) {
        if (errno != EINTR) {
            fprintf(stderr, "Failed to set socket options! \n%s\n", strerror(errno));
            return(EXIT_FAILURE);
        }
    }

    while (listen(sockfd, MAX_CONNECTIONS)) {
        if (errno != EINTR) {
            fprintf(stderr, "Failed to listen on port! \n%s\n", strerror(errno));
            return(EXIT_FAILURE);
        }
    }

    FILE* sockfile;
    int connfd;
    struct sockaddr_in in_addr;
    socklen_t in_addr_len = sizeof(struct sockaddr_in);
    while (!quit) {
        if ((connfd = accept(sockfd, (struct sockaddr *) &in_addr, &in_addr_len))<0) {
            fprintf(stderr, "Failed to accept! \n%s\n", strerror(errno));
            freeaddrinfo(ai);
            return(EXIT_FAILURE);
        }
        printf("Accepted connection %i\n", in_addr.sin_addr.s_addr);
        sockfile = fdopen(connfd, "r+");
        handle_connection(sockfile, documentroot, indexfile);
        fclose(sockfile);
    }

    printf("\nClosing Server and exiting\n");
    freeaddrinfo(ai);

    return(EXIT_SUCCESS);
}

int send_error(FILE* sockfile, int scode, char* title, char* text) {
    send_header(sockfile, scode, title);
    fprintf(sockfile, "<html><head><title>%i %s</title></head>", scode, title);
    fprintf(sockfile, "<body><p><b>%i:<b> %s</p></body></html>", scode, text);
    fflush(sockfile);
    return EXIT_SUCCESS;
}

int send_header(FILE* sockfile, int statuscode, char* statusname) {
    time_t now = time(NULL);
    char timebuf[128];
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));

    fprintf(sockfile, "HTTP/1.1 %i %s\r\n", statuscode, statusname);
    fprintf(sockfile, "Date: %s\r\n", timebuf);
    fprintf(sockfile, "Content-type: text/html\r\n");
    fprintf(sockfile, "Connection: close\r\n");
    fprintf(sockfile, "\r\n");

    return EXIT_SUCCESS;
}

int send_response(FILE* sockfile, char* filepath) {
    FILE* webpage;

    webpage=fopen(filepath, "r");
    if (!webpage) {
        printf("Error opening file\n");
        send_error(sockfile, 404, "Not Found", "File Not Found");
        return EXIT_FAILURE;
    }

    send_header(sockfile, 200, "OK");
    char data[2048];
    int n;

    while ((n = fread(data, 1, sizeof(data), webpage)) > 0) 
        fwrite(data, 1, n, sockfile);

    
    fflush(sockfile);
    printf("Finished sending response\n");
    return EXIT_SUCCESS;
}

int main (int argc, char **argv){

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));

	sa.sa_handler = handle_soft_exit;
    sigaction(SIGINT, &sa, NULL);

    	/* Argument Parsing */
	char *p_arg = NULL, *i_arg = NULL, *indexfile, *docroot, *port;
	int opt_p = 0, opt_i = 0, c;

	while((c=getopt(argc, argv, "p:i:")) != -1) {
 		switch (c){
 			case 'p':// port
 				opt_p++;
				p_arg = optarg;
 				break;
 			case 'i':// index file
 				opt_i++;
				i_arg = optarg;
 				break;

 			case '?':
 			default:// illegal arguments
 				usage(argv[0]);
 				break;
 		}
 	}
 	if (opt_p > 1 || opt_i > 1) {
 		fprintf(stderr, "Every option can only be used unce!\n");
 		usage(argv[0]);
 	}

	if (opt_p==0) port = "8080";
	else port = p_arg;

    if (opt_i==0) indexfile = "index.html";
    else indexfile = i_arg;

    if (argc-optind!=1) {
        fprintf(stderr, "A Document Root has to be specified\n");
        usage(argv[0]);
    }

    docroot = argv[optind];

    printf("Port: %s, Doc Root: %s, index: %s\n", port, docroot, indexfile);
    printf("http://localhost:%s\n", port);
    http_server(port, docroot, indexfile);

    return(EXIT_SUCCESS);
}
