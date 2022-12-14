#ifndef CONTROL_PROTOCOL_H
#define CONTROL_PROTOCOL_H

#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "../../include/stm.h"
#include "../../include/buffer.h"
#include "../parsers/cpAuthParser.h"
#include "../parsers/cpCommandParser.h"
#include "cpCommands.h"

#define BUFFER_SIZE 1024
#define HELLO_LEN 10

#define CONTROL_PROT_VERSION "0.1"
#define ADMIN_PASSWORD "pass1234"

#define TOKEN_DELIMITER ":"
#define LINE_DELIMITER "\n"

#define ON 1
#define OFF 0

typedef enum controlProtStmState {
    CP_HELLO,
    CP_AUTH,
    CP_EXECUTE,
    CP_OK,
    CP_ERROR
} controlProtStmState;

typedef enum controlProtStatus{
    STATUS_ERROR = '0',
    STATUS_SUCCESS
} controlProtStatus;

typedef enum controlProtErrorCode{
    CPERROR_INVALID_PASSWORD = '0',
    CPERROR_COMMAND_NEEDS_DATA,
    CPERROR_NO_DATA_COMMAND,
    CPERROR_INVALID_FORMAT,
    CPERROR_INEXISTING_USER,
    CPERROR_ALREADY_EXISTS,
    CPERROR_USER_LIMIT,
    CPERROR_GENERAL_ERROR     /* Encapsulamiento de los errores de memoria */
} controlProtErrorCode;


/* Estructura para manejar los datos de una conexion
    del protocolo de control */
typedef struct controlProtConn {
    int fd;
    int interests;
    fd_selector s;

    buffer * readBuffer;
    buffer * writeBuffer;

    uint8_t readBufferData[BUFFER_SIZE];
    uint8_t writeBufferData[BUFFER_SIZE];

    struct state_machine connStm;
    controlProtStmState currentState;

    cpAuthParser authParser;
    cpCommandParser commandParser;

    bool validPassword;

    /* Variables para manejar casos donde el protocolo ya hubiese escrito
        una respuesta al buffer de salida, pero TCP no la hubiera enviado aun */
    bool helloWritten;
    bool authAnsWritten;
    bool execAnsWritten;
    char * execAnswer;

    /* Puntero al siguiente. Se usa para liberar todo ante una terminacion normal */
    struct controlProtConn * nextConn;
} controlProtConn;

typedef struct cpConnList {
    controlProtConn * first;
    size_t size;
} cpConnList;

controlProtConn * newControlProtConn(int fd, fd_selector s);
void cpWriteHandler(struct selector_key * key);
void cpReadHandler(struct selector_key * key);
void cpCloseHandler(struct selector_key * key);
void freeControlProtConn(controlProtConn * cpc, fd_selector s);
void freeCpConnList();

#endif