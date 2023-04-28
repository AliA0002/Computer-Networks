#ifndef __DNS_CONNECTION_H_
#define __DNS_CONNECTION_H_

#include "DNSHeader.h"
#include "DNSQuestion.h"
#include "DNSRecord.h"
#include "Socket.h"
#include <assert.h>
#include <string>

using std::string;

class DNSConnection {
public:
  virtual string resolve(string query) = 0;
  virtual ~DNSConnection() {}
};

class NoDNS : public DNSConnection {
private:
  string web_sever_ip;

public:
  NoDNS(string ip);
  string resolve(string query) override;
};

class DNS : public DNSConnection {
private:
  string dns_ip;
  uint16_t dns_port;
  int x;

public:
  DNS(string ip, uint16_t port);
  string resolve(string query) override;
};
#endif