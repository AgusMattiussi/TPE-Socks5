#ifndef USER_MGMT_H
#define USER_MGMT_H

#include <stdint.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "../include/args.h"

enum add_user_state{
    ADD_OK,
    ADD_MAX_USERS,
    ADD_USER_EXISTS,
    ADD_ERROR
};

int process_authentication_request(char * username, char * password);
char * get_curr_user();
void set_curr_user(char * username);
uint8_t get_total_curr_users();
void free_curr_user();
enum add_user_state add_user(user_t * user);
bool needs_auth();
int remove_user(char * username);
int change_password(char * username, char * new_password);
int
user_exists(char * username, char * password);
user_t **
get_all_users();

#endif
