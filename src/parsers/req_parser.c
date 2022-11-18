#include "req_parser.h"

#define REQ_DST_PORT_BYTES 2

void 
req_parser_init(struct req_parser * parser){
    parser->state = REQ_VER;
    parser->to_parse = 0;
    parser->where_to = NULL;

    parser->cmd = REQ_CMD_NONE;
    parser->type = ADDR_TYPE_NONE;
    parser->port = 0x00;
}

void
set_dst_port_config(struct req_parser * parser){
    parser->to_parse = REQ_DST_PORT_BYTES;
    parser->state = REQ_DST_PORT;
    parser->where_to = (uint8_t *) &(parser->port);
}

void 
parse_addr_byte(struct req_parser * parser, uint8_t to_parse){
    *(parser->where_to++) = to_parse;

    parser->to_parse--;
    if (parser->to_parse == 0) {
        parser->to_parse = 2;
        parser->where_to = (uint8_t *)&(parser->port);
        parser->state = REQ_DST_PORT;
    }
}

void 
req_parse_byte(struct req_parser * parser, uint8_t to_parse){
    switch(parser->state){
        case REQ_VER:
            printf("[REQ_VER] byte to parse %d\n", to_parse);
            if(to_parse == SOCKS_VERSION) parser->state = REQ_CMD;
            else{
                fprintf(stdout, "Request has unsupported version.");
                parser->state = REQ_ERROR;
            }
            break;
        case REQ_CMD:
            printf("[REQ_CMD] byte to parse %d\n", to_parse);
            if(to_parse == REQ_CMD_CONNECT || to_parse == REQ_CMD_BIND ||
            to_parse == REQ_CMD_UDP){
                parser->cmd = to_parse;
                parser->state = REQ_RSV;
            }
            else{parser->state=REQ_ERROR;}
            break;
        case REQ_RSV:
            printf("[REQ_RSV] byte to parse %d\n", to_parse);
            if(to_parse == 0x00) parser->state = REQ_ATYP;
            else{parser->state = REQ_ERROR;}
            break;
        case REQ_ATYP:
            printf("[REQ_ATYP] byte to parse %d\n", to_parse);
            //We need to initialize according to the addrtype
            parser->type = to_parse;
            if(to_parse == IPv4){
                memset(&(parser->addr.ipv4), 0, sizeof(parser->addr.ipv4));
                parser->to_parse = IPv4_BYTES;
                parser->addr.ipv4.sin_family = AF_INET;
                parser->where_to = (uint8_t *)&(parser->addr.ipv4.sin_addr);

                parser->state = REQ_DST_ADDR;
            }
            else if(to_parse == IPv6){
                memset(&(parser->addr.ipv6), 0, sizeof(parser->addr.ipv6));
                parser->to_parse = IPv6_BYTES;
                parser->addr.ipv6.sin6_family = AF_INET6;
                parser->where_to = parser->addr.ipv6.sin6_addr.s6_addr;
                parser->state = REQ_DST_ADDR;
            }
            else if(to_parse == FQDN){
                parser->where_to = parser->addr.fqdn;
                parser->to_parse = -2; //TODO: Queda como magic number, es para indicar primer
                //parseo de FQDN
                parser->state = REQ_DST_ADDR;
            }
            else{
                fprintf(stdout, "Req: Wrong address type");
                parser->state = REQ_ERROR;
            }
            break;
        case REQ_DST_ADDR:
            printf("[REQ_DST_ADDR] byte to parse %d\n", to_parse);

            if(parser->type == FQDN && parser->to_parse == -2){
                // Tengo que leer los octetos exactos, el primero me dice cuantos tiene
                // Si no tengo nada, paso directo a REQ_DST_PORT
                if(to_parse == 0){
                    set_dst_port_config(parser);
                }
                else{
                    //Lo anoto porque si no después ni yo me voy a acordar porque esta así:
                    // Si hay bytes para parsear, me guardo cuantos son, delimito el final del
                    // string dado por la cantidad que tengo para leer, y paso a parsearlo
                    parser->to_parse = to_parse; //Quedo medio trabalengua
                    parser->addr.fqdn[parser->to_parse] = 0;
                    parser->state = REQ_DST_ADDR;
                }
                // Nota: no vuelve a entrar aca por (a) el state para a ser REQ_DST_PORT,
                // o (b) "parser->to_parse" pasa a ser un valor > -2 (algo para leer del FQDN);
            }
            else{
                parse_addr_byte(parser, to_parse);
            }
            break;
        case REQ_DST_PORT:
            printf("[REQ_DST_PORT] byte to parse %d\n", to_parse);
            *(parser->where_to++) = to_parse;
            parser->to_parse--;
            if (parser->to_parse == 0) {
                parser->state = REQ_DONE;
            }
            break;
        case REQ_DONE: case REQ_ERROR: break;
        default:
            fprintf(stdout, "Unrecognized request parsing state, ending parsing");
            break;
    }
}

enum req_state req_parse_full(struct req_parser * parser, buffer * buff){
    printf("Entro a req_parse_full\n");
    while(buffer_can_read(buff)){
        uint8_t to_parse = buffer_read(buff);
        req_parse_byte(parser, to_parse);
        if(parser->state == REQ_ERROR){
            fprintf(stdout, "Error parsing request, returning.");
            return REQ_ERROR;
        }
        if(parser->state == REQ_DONE){break;}
    }
    return parser->state;
}