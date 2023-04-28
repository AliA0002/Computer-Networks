#ifndef _PROXY_H_
#define _PROXY_H_
#include "args.h"
#include "log.h"
#include <Socket.h>
#include <iostream>
#include <vector>

void handle_connection(int client_fd, const args_t args, Log *l);

#endif