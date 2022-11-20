#ifndef CP_COMMANDS_H
#define CP_COMMANDS_H

#include "controlProtocol.h"

void addProxyUser(cpCommandParser * parser, char * answer);
void removeProxyUser(cpCommandParser * parser, char * answer);
void turnOnPassDissectors(cpCommandParser * parser, char * answer);
void turnOffPassDissectors(cpCommandParser * parser, char * answer);

#endif