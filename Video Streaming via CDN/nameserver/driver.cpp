

#include "DNSHeader.h"
#include "DNSQuestion.h"
#include "DNSRecord.h"
#include "Socket.h"
#include "params.h"
#include "utils.h"
#include <arpa/inet.h>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <getopt.h>
#include <iostream>
#include <limits>
#include <mutex>
#include <ostream>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

using std::cout;
using std::deque;
using std::endl;
using std::istream;
using std::lock_guard;
using std::mutex;
using std::ofstream;
using std::ostream;
using std::string;
using std::thread;
using std::vector;

ostream &operator<<(ostream &os, const DNSHeader &header) {
    os << header.ID << ' ' << header.QR << ' ' << header.OPCODE << ' ' << header.AA << ' ' << header.TC << ' '
       << header.RD << ' ' << header.RA << ' ' << (int)header.Z << ' ' << (int)header.RCODE << ' ' << header.QDCOUNT
       << ' ' << header.ANCOUNT << ' ' << header.NSCOUNT << ' ' << header.ARCOUNT;
    return os;
}

ostream &operator<<(ostream &os, const DNSQuestion &question) {
    os << question.QNAME << ' ' << question.QTYPE << ' ' << question.QCLASS;
    return os;
}
ostream &operator<<(ostream &os, const DNSRecord &record) {
    os << record.NAME << ' ' << record.TYPE << ' ' << record.CLASS << ' ' << record.TTL << ' ' << record.RDLENGTH << ' '
       << record.RDATA;
    return os;
}
int x = 0;
void run_request(int fd, string req_val) {
    socket_raii s(fd);

    // send header and question
    DNSHeader header;
    DNSQuestion question;
    header.ID = x;
    x++;
    header.QR = 0;
    header.OPCODE = 0;
    header.AA = 0;
    header.TC = 0;
    header.RD = 0;
    header.RA = 0;
    header.Z = 0;
    header.RCODE = 0;
    header.QDCOUNT = 1;
    header.ANCOUNT = 0;
    header.NSCOUNT = 0;
    header.ARCOUNT = 0;

    strcpy(question.QNAME, req_val.c_str());
    question.QTYPE = 1;
    question.QCLASS = 1;

    string header_string = DNSHeader::encode(header);
    string question_string = DNSQuestion::encode(question);

    int header_len = htonl(header_string.length());
    int question_len = htonl(question_string.length());

    char buf[BUF_SIZE];
    memset(buf, 0, BUF_SIZE);

    char *head = buf;
    memset(buf, 0, BUF_SIZE);
    memcpy(head, &header_len, 4);
    head += 4;
    memcpy(head, header_string.c_str(), header_string.length());
    head += header_string.length();
    memcpy(head, &question_len, 4);
    head += 4;
    memcpy(head, question_string.c_str(), question_string.length());
    head += question_string.length();
    socket_send_all(fd, buf, head - buf);

    std::cout << "len\t" << 4 << endl
              << "head\t" << header_string.length() << endl
              << "len\t" << 4 << endl
              << "question\t" << question_string.length() << endl
              << "sent " << head - buf << " bytes" << std::endl;

    // recv header and record
    memset(buf, 0, BUF_SIZE);
    socket_recv_all(fd, buf, 4);
    int header_len_recv = ntohs(*(int *)buf);
    socket_recv_all(fd, buf, header_len_recv);
    string header_string_recv(buf, header_len_recv);
    DNSHeader header_recv = DNSHeader::decode(header_string_recv);
    memset(buf, 0, BUF_SIZE);
    socket_recv_all(fd, buf, 4);
    int record_len_recv = ntohs(*(int *)buf);
    socket_recv_all(fd, buf, record_len_recv);
    string record_string_recv(buf, record_len_recv);
    DNSRecord record_recv = DNSRecord::decode(record_string_recv);
    cout << header_recv << endl << record_recv << endl;
}

int main() {
    int fd = make_sock("localhost", 80);
    run_request(fd, "video.cse.umich.edu");
    return 0;
}