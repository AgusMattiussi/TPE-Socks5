#ifndef CONN_HANDLER_H
#define CONN_HANDLER_H

#include "stm.h"
#include "../socks5/socks5.h"
#include "selector.h"
//FIXME: #include "../parsers/conn_parser.h"


/*

Works as a helper function for the STM  

*/

void close_socks5_connection(struct socks_conn_model * connection);

void socks_connection_read(struct selector_key * key);
void socks_connection_write(struct selector_key * key);
void socks_connection_block(struct selector_key * key);
void socks_connection_close(struct selector_key * key);

#endif