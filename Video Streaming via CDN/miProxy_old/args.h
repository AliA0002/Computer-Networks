#ifndef _ARGS_H_
#define _ARGS_H_
#include <string>
using std::string;
struct args_t {
    bool dns;
    uint16_t listen_port;
    // if !dns
    string web_server_ip;
    // else if dns
    string dns_ip;
    uint16_t dns_port;

    float alpha;
    string logfile;
};

#endif