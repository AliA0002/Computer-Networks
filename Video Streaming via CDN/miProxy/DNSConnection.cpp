
#include "DNSConnection.h"

using std::string;

NoDNS::NoDNS(string ip) : web_sever_ip(ip) {}

string NoDNS::resolve(string query) { return web_sever_ip; }

DNS::DNS(string ip, uint16_t port) : dns_ip(ip), dns_port(port) {}

string DNS::resolve(string query) {
    int dnsfd = make_sock(dns_ip.c_str(), dns_port);
    socket_raii s(dnsfd);

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

    strcpy(question.QNAME, query.c_str());
    question.QTYPE = 1;
    question.QCLASS = 1;

    string header_string = DNSHeader::encode(header);
    string question_string = DNSQuestion::encode(question);

    int header_len = htonl(header_string.length());
    int question_len = htonl(question_string.length());

    char buf[BUF_SIZE];

    socket_send(dnsfd, &header_len, sizeof(header_len));
    socket_send(dnsfd, header_string.c_str(), header_string.length());
    socket_send(dnsfd, &question_len, sizeof(question_len));
    socket_send(dnsfd, question_string.c_str(), question_string.length());

    // recv header and record
    memset(buf, 0, BUF_SIZE);
    int len;
    socket_recv_all(dnsfd, &len, sizeof(len));
    int header_len_recv = ntohl(len);
    socket_recv_all(dnsfd, buf, header_len_recv);
    string header_string_recv(buf, header_len_recv);
    DNSHeader header_recv = DNSHeader::decode(header_string_recv);
    memset(buf, 0, BUF_SIZE);
    socket_recv_all(dnsfd, &len, sizeof(len));
    int record_len_recv = ntohl(len);
    socket_recv_all(dnsfd, buf, record_len_recv);
    string record_string_recv(buf, record_len_recv);
    DNSRecord record_recv = DNSRecord::decode(record_string_recv);
    assert(header_recv.RCODE != 3);
    return string(record_recv.RDATA, record_recv.RDATA + record_recv.RDLENGTH);
}