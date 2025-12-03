#ifndef SERVER_USER_LIST_H
#define SERVER_USER_LIST_H

void send_user_list(int client_fd);
void register_user(int client_fd, const char *username);

#endif
