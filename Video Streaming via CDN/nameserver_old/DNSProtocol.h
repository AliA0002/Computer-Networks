#ifndef _DNS_PROTOCOL_H_
#define _DNS_PROTOCOL_H_

#include "DNSHeader.h"
#include "DNSQuestion.h"
#include "DNSRecord.h"
#include <netinet/in.h>
#include <string>

using namespace std;

// For Part 2
struct Query {
    DNSHeader header;
    DNSQuestion question;
};

struct Response {
    DNSHeader header;
    DNSRecord record;
};

class response {
  public:
    response(string hostname, ushort query_id) {
        this->hostname = hostname;
        this->query_id = query_id;
    }
    Response get_buffer();
    string hostname, ip;

  private:
    DNSHeader header;
    DNSRecord record;
    ushort query_id;
    Response buffer;
};

#endif