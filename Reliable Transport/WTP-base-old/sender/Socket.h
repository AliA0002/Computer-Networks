
#ifndef _SOCKET_WRAPPER_H_
#define _SOCKET_WRAPPER_H_

#include <arpa/inet.h> // htons()
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <stdexcept>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/socket.h> // socket(), bind(), listen(), accept()
#include <unistd.h>     // close()

int socket_init(int);
int socket_recv(int, void *, size_t);
int socket_recv_all(int, void *, size_t);
int socket_send(int, const void *, size_t);
int socket_send_all(int, const void *, size_t);
int socket_send(int, std::string);
int socket_listen(int, int);
int socket_accept(int);
int socket_close(int);

int socket_getPort(int);

void makeSockAddr(struct sockaddr_in *, int);

// Client stuff
struct host_t {
  char *hostname;
  int port;
  host_t(char *hostname, int port);
};
int make_sockaddr(struct sockaddr_in *, const char *, int);
int make_sock(const char *, int);
int make_sock(host_t);

std::string get_ip_addr(int);

struct socket_raii {
  int fd;
  socket_raii(int fd) : fd(fd) {}
  ~socket_raii() { socket_close(fd); }
};

#endif