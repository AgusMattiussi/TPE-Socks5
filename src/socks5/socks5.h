#ifndef SOCKS5_H
#define SOCKS5_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/types.h>  
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#include "../include/stm.h"
#include "../include/buffer.h"
#include "../include/selector.h"
#include "../include/server.h"
#include "../parsers/conn_parser.h"
#include "../parsers/auth_parser.h"
#include "../parsers/req_parser.h"
#include "../users/user_mgmt.h" 


#define N(x) (sizeof(x)/sizeof((x)[0]))

enum socks_state{
    //HELLO_READ,
    //HELLO_WRITE,
    CONN_READ,
    CONN_WRITE,
    AUTH_READ,
    AUTH_WRITE,
    REQ_READ,
    REQ_WRITE,
    REQ_RESOLVE,
    REQ_CONNECT,
    COPY,
    ERROR,
    DONE
};

struct std_conn_model{
    struct sockaddr_storage addr;
    socklen_t addr_len;
    int socket;
    int interests;
};

struct buffers_t{
    buffer read_buff; // CLI -> SRV
    buffer write_buff; // SRV -> CLI

    uint8_t * aux_read_buff;
    uint8_t * aux_write_buff;
};

struct copy_model_t{
    int fd;
    struct buffers_t * buffers;
    fd_interest interests;
    fd_interest connection_interests;
    struct copy_model_t * other;
};

struct parsers_t{
    struct conn_parser * connect_parser;
    struct auth_parser * auth_parser;
    struct req_parser * req_parser;
};

typedef struct socks_conn_model{

    struct std_conn_model * cli_conn;
    struct std_conn_model * src_conn;
    int src_addr_family;

    struct buffers_t * buffers;
    struct parsers_t * parsers;

    struct addrinfo * resolved_addr;
    struct addrinfo * curr_addr;

    struct state_machine stm;
    // POP3?

    struct copy_model_t * cli_copy;
    struct copy_model_t * src_copy;

} socks_conn_model;

struct state_definition * socks5_all_states();

//TODO: pasar a mng.c
struct state_definition * mng_all_states();
uint32_t socks_get_buf_size();

void close_socks5_connection(socks_conn_model * connection);

#endif