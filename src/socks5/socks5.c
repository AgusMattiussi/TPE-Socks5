#include "socks5.h"

#define CLI 0
#define SRC 1
#define BUFF_SIZE 2048


/*----------------------
 |  Connection functions
 -----------------------*/

static int 
check_buff_and_receive(buffer * buff_ptr, int socket){
    size_t byte_n;
    uint8_t * write_ptr = buffer_write_ptr(buff_ptr, &byte_n);
    ssize_t n_received = recv(socket, write_ptr, byte_n, 0); //TODO:Flags?

    if(n_received <= 0) return -1;
    buffer_write_adv(buff_ptr, n_received);
    return n_received;
}

static int 
check_buff_and_send(buffer * buff_ptr, int socket){
    size_t n_available;
    uint8_t * read_ptr = buffer_read_ptr(buff_ptr, &n_available);
    ssize_t n_sent = send(socket, read_ptr, n_available, 0); //TODO: Flags?
    if(n_sent == -1){
        LogError("Error sending bytes to client socket.");
        return -1;
    }
    buffer_read_adv(buff_ptr, n_sent);
    return n_sent;
}

void conn_read_init(const unsigned state, struct selector_key * key){
    struct socks_conn_model * connection = (socks_conn_model *)key->data;
    start_connection_parser(connection->parsers->connect_parser);
}

static enum socks_state conn_read(struct selector_key * key){
    struct socks_conn_model * connection = (socks_conn_model *)key->data;
    struct conn_parser * parser = connection->parsers->connect_parser;

    if(check_buff_and_receive(&connection->buffers->read_buff,
                                    connection->cli_conn->socket) == -1){ return ERROR; }

    enum conn_state ret_state = conn_parse_full(parser, &connection->buffers->read_buff);
    if(ret_state == CONN_ERROR){
        LogError("Error while parsing.");
        return ERROR;
    }
    if(ret_state == CONN_DONE){
        selector_status ret_selector = selector_set_interest_key(key, OP_WRITE);
        if(ret_selector == SELECTOR_SUCCESS){
            size_t n_available;
            uint8_t * write_ptr = buffer_write_ptr(&connection->buffers->write_buff, &n_available);
            if(n_available < 2){
                LogError("Not enough space to send connection response.");
                return ERROR;
            }
            *write_ptr++ = SOCKS_VERSION; *write_ptr = parser->auth;
            buffer_write_adv(&connection->buffers->write_buff, 2);
            return CONN_WRITE;
        }
        return ERROR;
    }
    return CONN_READ;
}


static enum socks_state 
conn_write(struct selector_key * key){
    socks_conn_model * connection = (socks_conn_model *) key->data;

    if(check_buff_and_send(&connection->buffers->write_buff, connection->cli_conn->socket) == -1){
        LogError("Error sending bytes to client socket.");
        return ERROR;
    }

    if(buffer_can_read(&connection->buffers->write_buff)){
        return CONN_WRITE;
    }

    selector_status status = selector_set_interest_key(key, OP_READ);
    if(status != SELECTOR_SUCCESS) return ERROR;

    switch(connection->parsers->connect_parser->auth){
        case NO_AUTH:
            LogDebug("STM pasa a estado REQ_READ\n");
            return REQ_READ;
        case USER_PASS:
            LogDebug("STM pasa a estado AUTH_READ\n");
            return AUTH_READ;
        case GSSAPI:
            LogDebug("GSSAPI is out of this project's scope.");
            return DONE;
        case NO_METHODS:
            return DONE;
    }
    return ERROR;
}

/*----------------------------
 |  Authentication functions
 ---------------------------*/


 void auth_read_init(const unsigned state, struct selector_key * key){
    socks_conn_model * connection = (socks_conn_model *)key->data;
    auth_parser_init(connection->parsers->auth_parser);
 }

 static enum socks_state 
 auth_read(struct selector_key * key){
    socks_conn_model * connection = (socks_conn_model *)key->data;
    struct auth_parser * parser = connection->parsers->auth_parser;

    if(check_buff_and_receive(&connection->buffers->read_buff,
                                    connection->cli_conn->socket) == -1){ return ERROR; }

    enum auth_state ret_state = auth_parse_full(parser, &connection->buffers->read_buff);
    if(ret_state == AUTH_ERROR){
        LogError("Error parsing auth method");
        return ERROR;
    }
    if(ret_state == AUTH_DONE){ 
        uint8_t is_authenticated = process_authentication_request((char*)parser->username, 
                                                                  (char*)parser->password);
        if(is_authenticated == -1){
            LogError("Error authenticating user. Username or password are incorrect, or user does not exist. Exiting.\n");
            return ERROR;
        }
        set_curr_user((char*)parser->username);
        selector_status ret_selector = selector_set_interest_key(key, OP_WRITE);
        if(ret_selector != SELECTOR_SUCCESS) return ERROR;        
        size_t n_available;
        uint8_t * write_ptr = buffer_write_ptr(&connection->buffers->write_buff, &n_available);
        if(n_available < 2){
            LogError("Not enough space to send connection response.");
            return ERROR;
        }
        write_ptr[0] = AUTH_VERSION;
        write_ptr[1] = is_authenticated;
        buffer_write_adv(&connection->buffers->write_buff, 2);
        return AUTH_WRITE;
    }
    return AUTH_READ;
}

static enum socks_state 
auth_write(struct selector_key * key){
    socks_conn_model * connection = (socks_conn_model *)key->data;

    if(check_buff_and_send(&connection->buffers->write_buff, connection->cli_conn->socket) == -1){
        LogError("Error sending bytes to client socket.");
        return ERROR;
    }

    if(buffer_can_read(&connection->buffers->write_buff)){
        return AUTH_WRITE;
    }
    selector_status ret_selector = selector_set_interest_key(key, OP_READ);
    return ret_selector == SELECTOR_SUCCESS? REQ_READ:ERROR;
}

 /*----------------------------
 |  Request functions
 ---------------------------*/

 #define FIXED_RES_BYTES 6

static int
req_response_message(buffer * write_buff, struct res_parser * parser){
    size_t n_bytes;
    uint8_t * buff_ptr = buffer_write_ptr(write_buff, &n_bytes);
    uint8_t * addr_ptr = NULL; 
    enum req_atyp addr_type = parser->type;

    size_t length;
    if(addr_type == IPv4){
        length = IPv4_BYTES;
        addr_ptr = (uint8_t *)&(parser->addr.ipv4.sin_addr);
    }
    else if(addr_type == IPv6){
        length = IPv6_BYTES;
        addr_ptr = parser->addr.ipv6.sin6_addr.s6_addr;
    }
    else if(addr_type == FQDN){
        length = strlen((char *)parser->addr.fqdn);
        addr_ptr = parser->addr.fqdn;
    }
    else{
        LogError("Address type not recognized\n");
        return -1;
    }  
    size_t space_needed = length + FIXED_RES_BYTES + (parser->type==FQDN);
    if (n_bytes < space_needed || addr_ptr == NULL) {
        return -1;
    }
    *buff_ptr++ = SOCKS_VERSION;
    *buff_ptr++ = parser->state;
    *buff_ptr++ = 0x00;
    *buff_ptr++ = addr_type;
    if (addr_type == FQDN) {
        *buff_ptr++ = length;
    }
    strncpy((char *)buff_ptr, (char *)addr_ptr, length);
    buff_ptr += length;
    uint8_t * port_ptr = (uint8_t *)&(parser->port);
    *buff_ptr++ = port_ptr[0];
    *buff_ptr++ = port_ptr[1];

    buffer_write_adv(write_buff, (ssize_t)space_needed);
    return (int)space_needed;
}

static void
set_res_parser(struct req_parser * parser, enum socks_state socks_state){
    parser->res_parser.state = socks_state;
    parser->res_parser.type = parser->type;
    parser->res_parser.port = parser->port;
    parser->res_parser.addr = parser->addr;
}

static enum socks_state 
manage_req_error(struct req_parser * parser, enum socks_state socks_state,
                socks_conn_model * conn, struct selector_key * key) {
    set_res_parser(parser, socks_state);
    selector_status selector_ret = selector_set_interest(key->s, conn->cli_conn->socket, OP_WRITE);
    int response_created = req_response_message(&conn->buffers->write_buff, &parser->res_parser);
    return ((selector_ret == SELECTOR_SUCCESS) && (response_created != -1))?REQ_WRITE:ERROR;
}

static enum socks_state 
init_connection(struct req_parser * parser, socks_conn_model * connection, struct selector_key * key) {
    connection->src_conn->socket = socket(connection->src_addr_family, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (connection->src_conn->socket == -1) {
        return ERROR;
    }
    int connect_ret = connect(connection->src_conn->socket, 
        (struct sockaddr *)&connection->src_conn->addr, connection->src_conn->addr_len);
    if(connect_ret == 0 || ((connect_ret != 0) && (errno == EINPROGRESS))){ 
        int selector_ret = selector_set_interest(key->s, connection->cli_conn->socket, OP_NOOP);
        if(selector_ret != SELECTOR_SUCCESS) {return ERROR;}
        selector_ret = selector_register(key->s, connection->src_conn->socket, 
                            get_conn_actions_handler(), OP_WRITE, connection);
        if(selector_ret != SELECTOR_SUCCESS){return ERROR;}
        return REQ_CONNECT;
    }
    LogError("Initializing connection failure");
    perror("Connect failed due to: ");
    return manage_req_error(parser, errno_to_req_response_state(errno), connection, key);
}

static struct addrinfo hint = {
    .ai_family = AF_UNSPEC,
    .ai_socktype = SOCK_STREAM,
    .ai_flags = AI_PASSIVE,
    .ai_protocol = 0,
    .ai_canonname = NULL,
    .ai_addr = NULL,
    .ai_next = NULL
};

struct addrinfo 
get_hint(){
    return hint;
}

static void 
clean_hint(){
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_PASSIVE;
    hint.ai_protocol = 0;
    hint.ai_canonname = NULL;
    hint.ai_addr = NULL;
    hint.ai_next = NULL;
}

static void *
req_resolve_thread(void * arg){
    struct selector_key * aux_key = (struct selector_key *) arg; 
    socks_conn_model * connection = (socks_conn_model *)aux_key->data;
    pthread_detach(pthread_self());
    char aux_buff[7];
    snprintf(aux_buff, sizeof(aux_buff), "%d", ntohs(connection->parsers->req_parser->port));
    int ret_getaddrinfo = -1;
    struct addrinfo aux_hint = get_hint();
    ret_getaddrinfo = getaddrinfo((char *) connection->parsers->req_parser->addr.fqdn,
                    aux_buff, &aux_hint, &connection->resolved_addr);
    if(ret_getaddrinfo != 0){
        LogError("Could not resolve FQDN.");
        freeaddrinfo(connection->resolved_addr);
        connection->resolved_addr = NULL;
    }
    clean_hint();
    connection->curr_addr = connection->resolved_addr;
    selector_notify_block(aux_key->s, aux_key->fd);
    free(arg);
    return 0;
}

static void 
req_read_init(unsigned state, struct selector_key * key) {
    socks_conn_model * conn = (socks_conn_model *)key->data;
    req_parser_init(conn->parsers->req_parser);
}

static enum socks_state
set_connection(socks_conn_model * connection, struct req_parser * parser, enum req_atyp type,
                struct selector_key * key){
    if(type == IPv4){
        connection->src_addr_family = AF_INET;
        parser->addr.ipv4.sin_port = parser->port;
        connection->src_conn->addr_len = sizeof(parser->addr.ipv4);
        memcpy(&connection->src_conn->addr, &parser->addr.ipv4, sizeof(parser->addr.ipv4));
    }
    else if(type == IPv6){
        connection->src_addr_family = AF_INET6;
        parser->addr.ipv6.sin6_port = parser->port;
        connection->src_conn->addr_len = sizeof(parser->addr.ipv6);
        memcpy(&connection->src_conn->addr, &parser->addr.ipv6, sizeof(parser->addr.ipv6));
    }
    else if(type == FQDN){
        struct selector_key * aux_key = malloc(sizeof(*key));
        if (aux_key == NULL) {
            LogError("Malloc failure for aux_key instantiation\n");
            return manage_req_error(parser, RES_SOCKS_FAIL, connection, key);
        }
        memcpy(aux_key, key, sizeof(*key));
        pthread_t tid;
        int thread_create_ret = pthread_create(&tid, NULL, &req_resolve_thread, aux_key);
        if (thread_create_ret !=0 ){
            free(aux_key);
            return manage_req_error(parser, RES_SOCKS_FAIL, connection, key);
        }
        selector_status selector_ret = selector_set_interest_key(key, OP_NOOP);
        if (selector_ret != SELECTOR_SUCCESS) { return ERROR; }
        return REQ_RESOLVE;
    }
    else{
        LogError("Unknown connection type\n");
        return ERROR;
    }
    return init_connection(parser, connection, key);

}

static enum socks_state 
req_read(struct selector_key * key) {
    socks_conn_model * connection = (socks_conn_model *)key->data;
    struct req_parser * parser = connection->parsers->req_parser;

    if(check_buff_and_receive(&connection->buffers->read_buff,
                                    connection->cli_conn->socket) == -1){ return ERROR; }

    enum req_state parser_state = req_parse_full(parser, &connection->buffers->read_buff);
    if (parser_state == REQ_DONE) {
        switch (parser->cmd) {
            case REQ_CMD_CONNECT:
                return set_connection(connection, parser, parser->type, key);
            case REQ_CMD_BIND:
            case REQ_CMD_UDP:
                return manage_req_error(parser, RES_CMD_UNSUPPORTED, connection, key);
            case REQ_CMD_NONE:
                return DONE;
            default:
                LogError("Unknown request command type\n");
                return ERROR;
        }
    }
    if (parser_state == REQ_ERROR){ return ERROR; }
    return REQ_READ;
}

static enum socks_state 
req_resolve(struct selector_key * key) {
    socks_conn_model * connection = (socks_conn_model *)key->data;
    struct req_parser * parser = connection->parsers->req_parser;

    if (connection->curr_addr == NULL) {
        if (connection->resolved_addr != NULL) {
            freeaddrinfo(connection->resolved_addr);
            connection->resolved_addr = NULL;
            connection->curr_addr = NULL;
        }
        return manage_req_error(parser, RES_HOST_UNREACHABLE, connection, key);
    }

    connection->src_addr_family = connection->curr_addr->ai_family;
    connection->src_conn->addr_len = connection->curr_addr->ai_addrlen;
    memcpy(&connection->src_conn->addr, connection->curr_addr->ai_addr,
           connection->curr_addr->ai_addrlen);
    connection->curr_addr = connection->curr_addr->ai_next;

    return init_connection(parser, connection, key);
}

static void
clean_resolved_addr(socks_conn_model * connection){
    freeaddrinfo(connection->resolved_addr);
    connection->resolved_addr = NULL;
}

static int
set_response(struct req_parser * parser, int addr_family, socks_conn_model * connection){
    parser->res_parser.state = RES_SUCCESS;
    parser->res_parser.port = parser->port;
    switch (addr_family) {
        case AF_INET:
            parser->res_parser.type = IPv4;
            memcpy(&parser->res_parser.addr.ipv4, &connection->src_conn->addr,
                sizeof(parser->res_parser.addr.ipv4));
            break;
        case AF_INET6:
            parser->res_parser.type = IPv6;
            memcpy(&parser->res_parser.addr.ipv6, &connection->src_conn->addr,
                sizeof(parser->res_parser.addr.ipv6));
            break;
        default:
            return -1;
    }
    return 0;
}

static enum socks_state 
req_connect(struct selector_key * key) {
    socks_conn_model * connection = (socks_conn_model *)key->data;
    struct req_parser * parser = connection->parsers->req_parser;
    int optval = 0;
    int getsockopt_ret = getsockopt(connection->src_conn->socket, SOL_SOCKET, 
                            SO_ERROR, &optval, &(socklen_t){sizeof(int)});
    if(getsockopt_ret == 0){
        if(optval != 0){
            if (parser->type == FQDN) {
                selector_unregister_fd(key->s, connection->src_conn->socket, false);
                close(connection->src_conn->socket);
                return req_resolve(key);
            }
            return manage_req_error(parser, errno_to_req_response_state(optval), connection, key);
        }
        if(parser->type == FQDN){ clean_resolved_addr(connection);}
        int ret_val = set_response(parser, connection->src_addr_family, connection);
        if(ret_val == -1){ return manage_req_error(parser, RES_SOCKS_FAIL, connection, key);}
        selector_status selector_ret = selector_set_interest_key(key, OP_NOOP);
        if(selector_ret == 0){
            selector_ret = selector_set_interest(key->s, connection->cli_conn->socket, OP_WRITE);
            if(selector_ret == 0){
                ret_val = req_response_message(&connection->buffers->write_buff, &parser->res_parser);
                if(ret_val != -1){ return REQ_WRITE; }
            }
        }
        return ERROR;
    }
    if(parser->type == FQDN){ clean_resolved_addr(connection);}
    return manage_req_error(parser, RES_SOCKS_FAIL, connection, key);
}

static enum socks_state 
req_write(struct selector_key * key) {
    socks_conn_model * connection = (socks_conn_model *)key->data;
    struct req_parser * parser = connection->parsers->req_parser;

    if(check_buff_and_send(&connection->buffers->write_buff, connection->cli_conn->socket) == -1){
        LogError("Error sending bytes to client socket.");
        return ERROR;
    }


    conn_information(connection);
    if(buffer_can_read(&connection->buffers->write_buff)){ return REQ_WRITE; }
    if(parser->res_parser.state != RES_SUCCESS){return DONE; }
    selector_status selector_ret = selector_set_interest_key(key, OP_READ);
    if(selector_ret == SELECTOR_SUCCESS){
        selector_ret = selector_set_interest(key->s, connection->src_conn->socket, OP_READ);
        return selector_ret == SELECTOR_SUCCESS?COPY:ERROR;
    }
    return ERROR;
}

static int
init_copy_structure(socks_conn_model * connection, struct copy_model_t * copy,
                    int which){
    if(which == CLI){
        copy->fd = connection->cli_conn->socket;
        copy->read_buff = &connection->buffers->read_buff;
        copy->write_buff = &connection->buffers->write_buff;
        copy->other = &connection->src_copy;
    }
    else if(which == SRC){
        copy->fd = connection->src_conn->socket;
        copy->read_buff = &connection->buffers->write_buff;
        copy->write_buff = &connection->buffers->read_buff;
        copy->other = &connection->cli_copy;
    }
    else{
        LogError("Error initializng copy structures\n");
        return -1;
    }
    copy->interests = OP_READ;
    copy->connection_interests = OP_READ | OP_WRITE;
    return 0;
}

static void 
copy_init(unsigned state, struct selector_key * key) {
    socks_conn_model * connection = (socks_conn_model *)key->data;
    struct copy_model_t * copy = &connection->cli_copy;
    int init_ret = init_copy_structure(connection, copy, CLI);
    if(init_ret == -1){
        fprintf(stdout, "Error initializng copy structures\n");
        //return ERROR;
    }
    copy = &connection->src_copy;
    init_ret = init_copy_structure(connection, copy, SRC);
    if(init_ret == -1){
        LogError("Error initializng copy structures\n");
        //return ERROR;
    }

    if(sniffer_is_on()){
        connection->pop3_parser = malloc(sizeof(pop3_parser));
        pop3_parser_init(connection->pop3_parser); 
        if(connection->pop3_parser == NULL)
            printf("QUILOMBO\n");    
    }
}

static struct copy_model_t *
get_copy(int fd, int cli_sock, int src_sock, socks_conn_model * connection){
    return fd == cli_sock? &connection->cli_copy:
           fd == src_sock? &connection->src_copy:
           NULL;
}

static enum socks_state 
copy_read(struct selector_key * key) {
    socks_conn_model * connection = (socks_conn_model *)key->data;
    struct copy_model_t * copy = get_copy(key->fd, connection->cli_conn->socket, 
                                    connection->src_conn->socket, connection);
    if(copy == NULL){
        LogError("Copy is null\n");
        return ERROR;
    }
    if(buffer_can_write(copy->write_buff)){
        int bytes_read = check_buff_and_receive(copy->write_buff, key->fd);
        if(bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)){
            return COPY;
        }

        if(bytes_read > 0){
            copy->other->interests = copy->other->interests | OP_WRITE;
            copy->other->interests = copy->other->interests & copy->other->connection_interests;
            selector_set_interest(key->s, copy->other->fd, copy->other->interests); //TODO: Capture error?

            if(ntohs(connection->parsers->req_parser->port) == POP3_PORT && 
                connection->pop3_parser != NULL && sniffer_is_on()){
                if(pop3_parse(connection->pop3_parser, copy->write_buff) == POP3_DONE){
                    pass_information(connection);
                }
            }
            return COPY;
        }

        copy->connection_interests = copy->connection_interests & ~OP_READ;
        copy->interests = copy->interests & copy->connection_interests;
        selector_set_interest(key->s, copy->fd, copy->interests); //TODO: Capture selector error?
        // https://stackoverflow.com/questions/570793/how-to-stop-a-read-operation-on-a-socket
        // man -s 2 shutdown
        shutdown(copy->fd, SHUT_RD);
        copy->other->connection_interests = 
            copy->other->connection_interests & OP_READ;
        
        if(!buffer_can_read(copy->write_buff)){
            copy->other->interests &= copy->other->connection_interests;
            selector_set_interest(key->s, copy->other->fd, copy->other->interests);
            shutdown(copy->other->fd, SHUT_WR);
        }


        return copy->connection_interests == OP_NOOP?
                (copy->other->connection_interests == OP_NOOP?
                DONE:COPY):COPY;
    }
    copy->interests = (copy->interests & OP_WRITE) & copy->connection_interests;
    selector_set_interest(key->s, key->fd, copy->interests);
    return COPY;
}

static enum socks_state 
copy_write(struct selector_key * key) {
    socks_conn_model * connection = (socks_conn_model *)key->data;
    struct copy_model_t * copy = get_copy(key->fd, connection->cli_conn->socket, 
                                    connection->src_conn->socket, connection);
    if(copy == NULL){
        LogError("Copy is null\n");
        return ERROR;
    }

    int bytes_sent = check_buff_and_send(copy->read_buff, key->fd);
    if(bytes_sent == -1){
        if(errno == EWOULDBLOCK || errno == EAGAIN){ return COPY; }
        LogError("Error sending bytes to client socket.");
        return ERROR;
    }

    //buffer_read_adv(copy->read_buff, bytes_sent);
    add_bytes_transferred((long)bytes_sent);
    copy->other->interests = (copy->other->interests | OP_READ) & copy->other->connection_interests;
    selector_set_interest(key->s, copy->other->fd, copy->other->interests); //TODO: Capture return?

    if (!buffer_can_read(copy->read_buff)) {
        copy->interests = (copy->interests & OP_READ) & copy->connection_interests;
        selector_set_interest(key->s, copy->fd, copy->interests);
        uint8_t still_write = copy->connection_interests & OP_WRITE;
        if(still_write == 0){
            shutdown(copy->fd, SHUT_WR);
        }
    }
    return COPY;
}

static void 
req_connect_init(){
//    printf("Estoy en estado REQ CONNECT\n");
}

static const struct state_definition states[] = {
    /*{
        .state = HELLO_READ,
    },
    {
        .state = HELLO_WRITE,
    },*/
    {
        .state = CONN_READ,
        .on_arrival = conn_read_init,
        .on_read_ready = conn_read,
    },
    {
        .state = CONN_WRITE,
        .on_write_ready = conn_write,
    },
    {
        .state = AUTH_READ,
        .on_arrival = auth_read_init,
        .on_read_ready = auth_read,
    },
    {
        .state = AUTH_WRITE,
        .on_write_ready = auth_write,
    },
    {
        .state = REQ_READ,
        .on_arrival = req_read_init,
        .on_read_ready = req_read,
    },
    {
        .state = REQ_WRITE,
        .on_write_ready = req_write,
    },
    {
        .state = REQ_RESOLVE,
        .on_block_ready = req_resolve,
    },
    {
        .state = REQ_CONNECT,
        .on_arrival = req_connect_init,
        .on_write_ready = req_connect,
    },
    {
        .state = COPY,
        .on_arrival = copy_init,
        .on_read_ready = copy_read,
        .on_write_ready = copy_write,
    },
    {
        .state = ERROR,
    },
    {
        .state = DONE,
    }
};

socks_conn_model * new_socks_conn() {

    socks_conn_model * socks = malloc(sizeof(struct socks_conn_model));
    if(socks == NULL) { 
        perror("error:");
        return NULL; 
    }
    memset(socks, 0x00, sizeof(*socks));

    socks->cli_conn = malloc(sizeof(struct std_conn_model));
    socks->src_conn = malloc(sizeof(struct std_conn_model));
    memset(socks->cli_conn, 0x00, sizeof(*(socks->cli_conn)));
    memset(socks->src_conn, 0x00, sizeof(*(socks->src_conn)));
    socks->cli_conn->interests = OP_READ;
    socks->src_conn->interests = OP_NOOP;

    socks->parsers = malloc(sizeof(struct parsers_t));
    memset(socks->parsers, 0x00, sizeof(*(socks->parsers)));

    socks->parsers->connect_parser = malloc(sizeof(struct conn_parser));
    socks->parsers->auth_parser = malloc(sizeof(struct auth_parser));
    socks->parsers->req_parser = malloc(sizeof(struct req_parser));
    memset(socks->parsers->connect_parser, 0x00, sizeof(*(socks->parsers->connect_parser)));
    memset(socks->parsers->auth_parser, 0x00, sizeof(*(socks->parsers->auth_parser)));
    memset(socks->parsers->req_parser, 0x00, sizeof(*(socks->parsers->req_parser)));

    socks->stm.initial = CONN_READ;
    socks->stm.max_state = DONE;
    socks->stm.states = states;
    stm_init(&socks->stm);

    socks->buffers = malloc(sizeof(struct buffers_t));
    socks->buffers->aux_read_buff = malloc((uint32_t)BUFF_SIZE);
    socks->buffers->aux_write_buff = malloc((uint32_t)BUFF_SIZE);

    buffer_init(&socks->buffers->read_buff, BUFF_SIZE, socks->buffers->aux_read_buff);
    buffer_init(&socks->buffers->write_buff, BUFF_SIZE, socks->buffers->aux_write_buff);

    return socks;
}

