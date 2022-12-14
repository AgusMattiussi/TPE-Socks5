#include <stdio.h>     /* for printf */
#include <stdlib.h>    /* for exit */
#include <limits.h>    /* LONG_MIN et al */
#include <string.h>    /* memset */
#include <errno.h>
#include <getopt.h>
#include <stdlib.h>

#include "include/args.h"
#include "logger/logger.h"
#include "users/user_mgmt.h"

static char * 
port(char * s) {
    char * end = 0;
    const long sl = strtol(s, &end, 10);

    if (end == s || '\0' != *end ||
        ((LONG_MIN == sl || LONG_MAX == sl) && ERANGE == errno) || sl < 0 ||
        sl > USHRT_MAX) {
        fprintf(stderr, "port should in in the range of 1-65536: %s\n", s);
        return NULL;
    }
    return s;
}

static void
user(char *s) {
    user_t * user = malloc(sizeof(user_t));

    char *p = strchr(s, ':');
    if(p == NULL) {
        fprintf(stderr, "password not found\n");
        exit(1);
    } else {
        *p = 0;
        p++;
        user->name = s;
        user->pass = p;
    }
    add_user(user);
}

static void
version(void) {
    fprintf(stderr, "SOCKSv5 version 0.1\n"
                    "ITBA Protocolos de Comunicación 2022/2 -- Grupo 3\n");
    exit(1);
}

static void
usage(const char * progname) {
    fprintf(stderr,
        "Usage: %s [OPTION]...\n"
        "\n"
        "   -h               Imprime la ayuda y termina.\n"
        "   -l <SOCKS addr>  Dirección donde servirá el proxy SOCKS.\n"
        "   -N               Deshabilita los passwords disectors.\n"
        "   -L <conf addr>   Dirección donde servirá el servicio de management.\n"
        "   -p <SOCKS port>  Puerto entrante conexiones SOCKS.\n"
        "   -P <conf port>   Puerto entrante conexiones configuracion\n"
        "   -u <name>:<pass> Usuario y contraseña de usuario que puede usar el proxy. Hasta 10.\n"
        "   -v               Imprime información sobre la versión versión y termina.\n"
        "   -m               Activa la opción de debugger.\n"
        "   -n               Desactiva la opción de debugger.\n"
        
        "\n",
        progname);
    exit(1);
}

void parse_args(int argc, char ** argv, struct socks5args * args) {
    memset(args, 0, sizeof(*args));

    args->socks_addr = NULL;
    args->socks_port = "1080";

    args->mng_addr = NULL;
    args->mng_port = "8080";

    int ret_code = 0;

    int c;
    while (true) {
        c = getopt(argc, argv, "hl:L:Np:P:U:u:vmn");
        if (c == -1)
            break;
        switch (c) {
            case 'h':
                usage("socks5d");
                    goto finally;
            case 'l':
                args->socks_addr = optarg;
                break;
            case 'L':
                args->mng_addr = optarg;
                break;
            case 'N':
                set_sniffer_state(false);
                break;
            case 'p':
                args->socks_port = port(optarg);
                if (args->socks_port == NULL) {
                    ret_code = 1;
                    goto finally;
                }
                break;
            case 'P':
                args->mng_port = port(optarg);
                if (args->mng_port == NULL) {
                    ret_code = 1;
                    goto finally;
                }
                break;
            case 'u': 
                user(optarg);
                break;
            case 'v':
                version();
                goto finally;
            case 'm':
                setLogOn();
                break;
            case 'n':
                setLogOff();
                break;
            default:
                fprintf(stderr, "unknown argument %d.\n", c);
                ret_code = 1;
                goto finally;
            }
    }
    if (optind < argc) {
        fprintf(stderr, "argument not accepted: ");
        while (optind < argc) {
            fprintf(stderr, "%s ", argv[optind++]);
        }
        fprintf(stderr, "\n");
        ret_code = 1;
        goto finally;
    }

finally:
    if (ret_code) {
        exit(ret_code);
    }
}
