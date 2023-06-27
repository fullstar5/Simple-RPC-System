/* Author: YiFei ZHANG 1174267
 * Author for create_listening_socket(): UniMelb COMP30023 week9 pratical
 * 
 * This is implementation file for rpc.h, implement simple RPC system
 * */

#define NONBLOCKING
#define _POSIX_C_SOURCE 200112L
#define _DEFAULT_SOURCE
#define INVALID -99999
#define TOOLONG 10000

#include "rpc.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <pthread.h>
#include <endian.h>


// ----------------------------STRUCTURES AND SELF DEFINED FUNCTIONS-----------------------------------
// function prototype
int create_listening_socket(char* service);
int handle_rpc_find(int newsockfd, rpc_server* srv);
int handle_rpc_call(int newsockfd, rpc_server* srv);
void* handle_client(void* arg_void);

// threads structure
typedef struct thread_arg_t{
	int newsockfd;
	rpc_server* srv;
}thread_arg_t;

// functions inside of server
typedef struct funct_t {
	char function_name[1001];
	rpc_handler handler;
}funct_t;

// rpc server structure
struct rpc_server {
	int new_socketfd;
	funct_t* functions;
	int funct_num;
	int funct_capacity;
};

struct rpc_client {
	int socketfd;

};

struct rpc_handle {
	char name[1001];
};


/* create_listening_socket
 * INPUT: char*
 * OUTPUT: int
 *
 * function from week9 pratical, used to create listening soket
 * */
int create_listening_socket(char* service) {
	int re, s, sockfd;
	struct addrinfo hints, *res;

	// Create address we're going to listen on (with given port number)
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;       // IPv6
	hints.ai_socktype = SOCK_STREAM; // Connection-mode byte streams
	hints.ai_flags = AI_PASSIVE;     // for bind, listen, accept
	// node (NULL means any interface), service (port), hints, res
	s = getaddrinfo(NULL, service, &hints, &res);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		exit(EXIT_FAILURE);
	}

	// Create socket
	sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sockfd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	// Reuse port if possible
	re = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &re, sizeof(int)) < 0) {
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}
	// Bind address to the socket
	if (bind(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}
	freeaddrinfo(res);

	return sockfd;
}


/* handle_rpc_find
 * INPUT: int, rpc_server*
 * OUTPUT: int
 *
 * function that response for handle 'find' request from client
 * */
int handle_rpc_find(int newsockfd, rpc_server* srv){
	int result = 0;
	char name[1001];
	int num_byte = read(newsockfd, name, 1000);
	name[num_byte] = '\0';
	for (int i = 0; i < srv -> funct_num; i++){
		if (strcmp(name, srv -> functions[i].function_name) == 0){
			result = 1;
			break;
		}
	}
	return result;
}


/* handle_rpc_call
 * INPUT: int, rpc_server*
 * OUTPUT: int
 *
 * funtion that response for handle 'call' request from client
 * */
int handle_rpc_call(int newsockfd, rpc_server* srv){
	if (srv == NULL){
		return 0;
	}

	uint64_t name_len;
	uint64_t network_name_len;
	char name[1001];
	// read function name
	read(newsockfd, &network_name_len, sizeof(uint64_t));
	name_len = be64toh(network_name_len);
	read(newsockfd, name, name_len);
	name[name_len] = '\0';

	// read data1
	int64_t data1;
	int64_t network_data1_receive;
	read(newsockfd, &network_data1_receive, sizeof(int64_t));
	data1 = be64toh(network_data1_receive);

	// read data2 size
	uint64_t network_data2_size_receive;
	uint64_t data2_size;
	read(newsockfd, &network_data2_size_receive, sizeof(uint64_t));
	data2_size = be64toh(network_data2_size_receive);

	// read data2
	void* data2 = malloc(data2_size);
	read(newsockfd, data2, data2_size);
	
	// assign value
	rpc_data* in = malloc(sizeof(rpc_data));  // need free
	assert(in);
	in -> data1 = data1;
	in -> data2 = data2;
	in -> data2_len = data2_size;
	
	int function_match_result = 0;
	for (int i = 0; i < srv -> funct_num; i++){
		if (strcmp(srv -> functions[i].function_name, name) == 0){
			rpc_data* out = srv -> functions[i].handler(in);
			if (out != NULL){
				// if data2 and data2_len in result is not consistent, this request is failed
				if ((out -> data2_len > 0 && out -> data2 == NULL) || (out -> data2_len <= 0 && out -> data2 != NULL)){
					return function_match_result;
				}
				function_match_result = 1;
				uint64_t network_data1_send = htobe64(out -> data1);
				write(newsockfd, &network_data1_send, sizeof(uint64_t));

				uint64_t network_data2_len_send = htobe64(out -> data2_len);
				write(newsockfd, &network_data2_len_send, sizeof(uint64_t));
				write(newsockfd, out -> data2, out -> data2_len);
				break;
			}
		}
	}

	return function_match_result;
}


/* handle_client
 * INPUT: void*
 * OUTPUT: void*
 *
 * main process funtion for multithread, keep processing request
 * */
void* handle_client(void* arg_void) {
	thread_arg_t* arg = (thread_arg_t*)arg_void;
	int newsockfd = arg->newsockfd;
	rpc_server* srv = arg->srv;
	
	while(1){
		char identifier;
		//read(srv -> new_socketfd, &identifier, 1);
		int identifier_read = read(newsockfd, &identifier, 1);
		if (identifier_read == 1){
		
			// rpc_find part
			if (identifier == 'F'){
				int result = handle_rpc_find(newsockfd, srv);
				int64_t network_result = htobe64(result);
				write(newsockfd, &network_result, sizeof(int64_t));
				
			}
			// rpc_call part
			else if (identifier == 'C'){
				int function_match_result = handle_rpc_call(newsockfd, srv);
				if (function_match_result == 0){
					int invalid_producre = INVALID;
					uint64_t network_invalid = htobe64(invalid_producre);
					write(newsockfd, &network_invalid, sizeof(uint64_t));		
				}
			}
			else{
				printf("invalid identifier: %c\n", identifier);
				int invalid_producre = INVALID;
				uint64_t network_invalid = htobe64(invalid_producre);
				write(newsockfd, &network_invalid, sizeof(uint64_t));
			}
		}
	}	
	close(newsockfd);
	free(arg);
	return NULL;
}
// ------------------------------END OF SELF DEFINE-------------------------------------------------------------


/* rpc_init_server
 * INPUT: int
 * OUTPUT: rpc_server *
 *
 * initalise server with given port number
 * */
rpc_server *rpc_init_server(int port) {
     	int sockfd;
	char ports[50];
	snprintf(ports, sizeof(ports), "%d", port);
	
	sockfd = create_listening_socket(ports);
	rpc_server* server = malloc(sizeof(rpc_server));
	assert(server != NULL);
	server->new_socketfd = sockfd;
	server->funct_num = 0;
	server->funct_capacity = 20;
	server->functions = malloc(server->funct_capacity * sizeof(funct_t));
	return server;
}


/* rpc_register
 * INPUT: rpc_server *, char *, rpc_handler
 * OUTPUT: int(-1 fail, 1 success)
 *
 * record new functions into server data, if function exist, replace with new function
 * */
int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {
	if (name == NULL || handler == NULL || srv == NULL){
		return -1;
	}
	// if function exist, replace with new function
	for (int i = 0; i < srv -> funct_num; i++){
		if (strcmp(srv -> functions[i].function_name, name) == 0){
			srv -> functions[i].handler = handler;
			return 1;
		}
	}
	// if all space have been full filled, realloc new space
	if (srv -> funct_num >= srv -> funct_capacity){
		srv -> funct_capacity *= 2;
		srv -> functions = realloc(srv -> functions, srv -> funct_capacity * sizeof(funct_t));
		assert(srv -> functions != NULL);
	}
	// add new functions in server
	strcpy(srv -> functions[srv -> funct_num].function_name, name);
	srv -> functions[srv -> funct_num].handler = handler;
	srv -> funct_num += 1;
	return 1;
}


/* rpc_serve_all
 * INPUT: rpc_server *
 * OUTPUT: --
 *
 * serve the request from cliet: rpc_find and rpc_call
 * */
void rpc_serve_all(rpc_server *srv) {
	if (srv == NULL){
		printf("srv is NULL");
		return;
	}
	
	struct sockaddr_in client_addr;
	socklen_t client_addr_size = sizeof client_addr;
	if (listen(srv->new_socketfd, 5) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	while (1){
		// multi-threads
		int newsockfd = accept(srv->new_socketfd, (struct sockaddr*)&client_addr, &client_addr_size);
		if (newsockfd < 0) {
			perror("accept");
			exit(EXIT_FAILURE);
		}
		// create argument for new threads
		pthread_t thread;
		thread_arg_t* thread_arg = malloc(sizeof(thread_arg_t));
		thread_arg -> newsockfd = newsockfd;
		thread_arg -> srv = srv;
		if (pthread_create(&thread, NULL, handle_client, (void*) thread_arg) != 0){
			perror("pthread_create");
			exit(EXIT_FAILURE);
		}
		// Detach
		if (pthread_detach(thread) != 0){
			perror("pthread_detach");
			exit(EXIT_FAILURE);
		}
	}
}


/* rpc_init_client
 * INPUT: char *, int
 * OUTPUT: rpc_client *
 *
 * Initialise client and connect to server
 * */
rpc_client *rpc_init_client(char *addr, int port) {
	int sockfd, s;
	struct addrinfo hints, *servinfo, *rp;
	char ports[50];

	// Create address
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;

	snprintf(ports, sizeof(ports), "%d", port);
	s = getaddrinfo(addr, ports, &hints, &servinfo);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		return NULL;
	}
	for (rp = servinfo; rp != NULL; rp = rp->ai_next) {
		sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sockfd == -1)
			continue;

		if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1)
			break;

		close(sockfd);
	}
	if (rp == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return NULL;
	}
	freeaddrinfo(servinfo);

	// create rpc client information
	rpc_client* client = malloc(sizeof(rpc_client));
	assert(client != NULL);
	client -> socketfd = sockfd;
	return client;
}


/* rpc_find
 * INPUT: rpc_client *, char *
 * OUTPUT: rpc_handle *
 *
 * find whether function exist in server
 * */
rpc_handle *rpc_find(rpc_client *cl, char *name) {

	// if any argumetns are null, return null
	if (cl == NULL || name == NULL){
		return NULL;
	}

	// send identifer before send real data
	char identifier = 'F';
	write(cl -> socketfd, &identifier, 1);

	// send name to find in server
	int n;
	n = write(cl -> socketfd, name, strlen(name));
	if (n != strlen(name)){
		printf("Error when sending name. rpc_find");
		return NULL;
	}
	
	// get result of find
	int64_t result;
	int64_t network_result;
	read(cl -> socketfd, &network_result, sizeof(int64_t));
	result = be64toh(network_result);
	if (result == 1){
		rpc_handle* handle = malloc(sizeof(rpc_handle));
		strcpy(handle -> name, name);
		return handle;
	}
	return NULL;
}


/* rpc_call
 * INPUT: rpc_client *, rpc_handle *, rpc_data *
 * OUTPUT: rpc_data *
 *
 * call the remote producre from server
 * */
rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {

	// if any parameters are NULL, this call fail
	if (cl == NULL || h == NULL || payload == NULL){
		return NULL;
	}
	// if data2 and data2_len is not consistent, this call fail
	if ((payload -> data2_len <= 0 && payload -> data2 != NULL) || (payload -> data2_len > 0 && payload -> data2 == NULL)){
		return NULL;
	}
	// if data2_len is too large to encode, this call fail
	if (payload -> data2_len > TOOLONG){
		fprintf(stderr, "Overlength error\n");
		return NULL;
	}

	// send identifer before send real data
	char identifier = 'C';
	write(cl -> socketfd, &identifier, 1);

	// send name length
	int name_len = strlen(h -> name);
	uint64_t network_name_len = htobe64(name_len);
	write(cl -> socketfd, &network_name_len, sizeof(uint64_t));

	// send function name
	write(cl -> socketfd, h -> name, strlen(h -> name));

	// send data1
	int64_t network_data1_send = htobe64(payload -> data1);
	write(cl -> socketfd, &network_data1_send, sizeof(int64_t));

	// send size of data2 and data2
	uint64_t network_data2_len_send = htobe64(payload -> data2_len);
	write(cl -> socketfd, &network_data2_len_send, sizeof(uint64_t));
	write(cl -> socketfd, payload -> data2, payload -> data2_len);
	
	// read result
	int64_t data1;
	int64_t network_data1_receive;
	read(cl -> socketfd, &network_data1_receive, sizeof(int64_t));
	//if received bad data
	data1 = be64toh(network_data1_receive);
	if (data1 == INVALID){
		return NULL;
	}
	// read data2 len
	uint64_t data2_len;
	uint64_t network_data2_len_receive;
	read(cl -> socketfd, &network_data2_len_receive, sizeof(uint64_t));
	data2_len = be64toh(network_data2_len_receive);
	void* data2 = malloc(data2_len);
	// read data2
	read(cl -> socketfd, data2, data2_len);
	// assign value
	rpc_data* out = malloc(sizeof(rpc_data));
	assert(out);
	out -> data1 = data1;
	out -> data2_len = 0;
	out -> data2 = NULL;
	if (data2_len != 0){
		out -> data2_len = data2_len;
		out -> data2 = data2;
	}
	return out;
}


/* rpc_close_client
 * INPUT: rpc_client *
 * OUTPUT: --
 *
 * Close the socket in client, then free the client
 * */
void rpc_close_client(rpc_client *cl) {
	if (cl == NULL || cl -> socketfd == -1){
		return;
	}
	close(cl -> socketfd);
	cl -> socketfd = -1;
	free(cl);
}


/**
 * rpc_data_free
 * INPUT: rpc_data *
 * OUTPUT: --
 *
 * free the resources in client
 */
void rpc_data_free(rpc_data *data) {
    if (data == NULL) {
        return;
    }
    if (data->data2 != NULL) {
        free(data->data2);
    }
    free(data);
}
