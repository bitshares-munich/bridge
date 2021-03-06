#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <curl/curl.h>

#include "bridge/book.h"
#include "bridge/vendor.h"
#include "bridge/market.h"
#include "utils/logging.h"


struct SocketDescriptors {
	int main_socket;
	int new_socket;
};

// globals
struct VendorList* vendor_list;

/**
 * Handle errors
 */
void error(const char *msg)
{
    perror(msg);
    exit(1);
}

/**
 * Handle SIGTERM gracefully
 */
volatile sig_atomic_t run = 1;
void terminate(int signum) {
	run = 0;
}

// function pointer
typedef char* (*function_pointer)(char*, size_t*);

/***
 * Retrieves a list of trading pairs in PROTOBUF format
 * @param command_line the command line
 * @param len the length of the response in bytes
 * @returns a char array that is the protobuf formatted response or NULL on error
 */
char* get_trading_pairs(char* command_line, size_t* len) {
	unsigned char* market_protobuf = NULL;
	struct Market* market_head = vendor_get_all_trading_pairs(vendor_list);
	if (market_head) {
		size_t market_protobuf_len = market_list_protobuf_encode_size(market_head);
		market_protobuf = (unsigned char*)malloc(market_protobuf_len);
		if (market_protobuf) {
			market_list_protobuf_encode(market_head, market_protobuf, market_protobuf_len, &market_protobuf_len);
			// send it across the wire.
			*len = market_protobuf_len;
			logit_int(LOGLEVEL_DEBUG, "Length of output: %d", *len);
		}
		market_free(market_head);
	}
	return (char*)market_protobuf;
}

/**
 * Given what the user passed, find the correct method to call
 * @param buffer what the user passed
 * @returns a function pointer that will handle the user request, or NULL if nothing found
 */
function_pointer getCommand(char* buffer) {
	if (strncmp(buffer, "trading_pairs", 13) == 0) {
		return get_trading_pairs;
	}
	return NULL;
}

/**
 * Append data to the current buffer
 */
int buffer_append(char** buffer, size_t curr_buffer_length, char* incoming, size_t incoming_length) {
	if (incoming_length > 0) {
		char* new_buffer = (char*)malloc(curr_buffer_length + incoming_length + 1);
		if (new_buffer == NULL)
			return -1;
		memcpy(new_buffer, *buffer, curr_buffer_length);
		memcpy(&new_buffer[curr_buffer_length], incoming, incoming_length);
		new_buffer[curr_buffer_length + incoming_length] = 0;
		free(*buffer);
		*buffer = new_buffer;
	}
	return curr_buffer_length + incoming_length;
}

/**
 * A connection has happend. This will handle it
 * @param threadarg a reference to the socket file descriptor
 * @returns nothing valuable
 */
void* do_connect(void* threadarg) {
	struct SocketDescriptors* sd = (struct SocketDescriptors*)threadarg;
	int sockfd = sd->new_socket;
	char *buffer = (char*)malloc(0);
	size_t buffer_length = 0;
	size_t n = 255;
	// read from the socket
	while (n >= 0 && n == 255) {
		char incoming[255];
		n = read(sockfd,incoming,255);
		if ((buffer_length = buffer_append(&buffer, buffer_length, incoming, n)) == -1)
			break;
		if (buffer_length > 65535)
			n = -1;
	}
	if (n < 0) {
		error("ERROR reading from socket");
	} else {
		if (strncmp(buffer, "EXIT", 4) == 0) {
			run = 0;
			shutdown(sd->main_socket, SHUT_RDWR); // wake up the socket so it shuts down
		} else {
			// figure out what the user wanted to do
			function_pointer command = getCommand(buffer);
			if (command != NULL) {
				size_t result_length = 0;
				char* result = command(buffer, &result_length);
				// write back to the socket
				if (result != NULL && result_length > 0) {
					n = write(sockfd,result, result_length);
					if (n < 0)
						error("ERROR writing to socket");
					free(result);
				} else {
					n = write(sockfd,"No Results",10);
					if (n < 0)
						error("ERROR writing to socket");
				}
			}
		}
	}
	// cleanup
	free(buffer);
	close(sockfd);
	free(sd);
	return NULL;
}

/**
 * Initialize the program
 */
int main_init() {
	vendor_list = vendors_get_all();

	return 0;
}

/**
 * Handle incoming connections
 */
int main_service_connections(int portno) {
    int sockfd, newsockfd;
    socklen_t clilen;

    struct sockaddr_in serv_addr, cli_addr;
    // open socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
       error("ERROR opening socket");
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
    	error("ERROR setting socket option");
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    // bind and listen
    if (bind(sockfd, (struct sockaddr *) &serv_addr,
             sizeof(serv_addr)) < 0)
             error("ERROR on binding");
    listen(sockfd,5);
    clilen = sizeof(cli_addr);
    while(run) {
		 // blocks until a connection
		 newsockfd = accept(sockfd,
					 (struct sockaddr *) &cli_addr,
					 &clilen);
		 if (run) {
			 if (newsockfd < 0)
				  error("ERROR on accept");
#ifndef SINGLE_THREADED
			 int retVal = 0;
			 // TODO: Implement thread pool
			 pthread_t thread_id;
			 struct SocketDescriptors* sd = (struct SocketDescriptors*)malloc(sizeof(struct SocketDescriptors));
			 if (sd == NULL)
				 return 0;
			 sd->main_socket = sockfd;
			 sd->new_socket = newsockfd;
			 if ( (retVal = pthread_create(&thread_id, NULL, do_connect, sd)) != 0) {
				 fprintf(stderr, "Error spawning thread. Return value: %d\n", retVal);
				 free(sd);
				 run = 0;
			 }
#else
			 do_connect(&newsockfd);
#endif
		 }
	 }
	logit(LOGLEVEL_ERROR, "Terminating...");
	if (vendor_list != NULL) {
		vendor_list_free(vendor_list, 1);
	}
	logit(LOGLEVEL_ERROR, "Threads shut down");
    logit(LOGLEVEL_ERROR, "Closing main socket");
    close(sockfd);
	return 0;
}

/**
 * Entry point
 */
int main(int argc, char *argv[])
{
     // handle SIGTERM
     struct sigaction action;
     memset(&action, 0, sizeof(struct sigaction));
     action.sa_handler = terminate;
     sigaction(SIGTERM, &action, NULL);

     // process command line
     if (argc < 2) {
         logit(LOGLEVEL_ERROR, "ERROR, no port provided");
         logit_string(LOGLEVEL_ERROR, "Syntax: %s PORTNO", argv[0]);
         exit(1);
     }

 	curl_global_init(CURL_GLOBAL_ALL);

     main_init();
     main_service_connections(atoi(argv[1]));

	curl_global_cleanup();

     return 0;
}
