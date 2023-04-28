#include "DNSProtocol.h"
#include "DNSRecord.h"

// There are some slight nuances in the format of our DNS messages. The main
// difference between what we do and what the RFC specifies is that the response
// should contain header + question + record, whereas our response is only
// header + record. Also, the size of each encoded object (represented as a
// 4-byte integer) is sent before sending the contents of the object. The
// overall procedure is outlined below:

// miProxy sends integer designating the size of DNS header -> miProxy sends DNS
// header via encode() -> miProxy sends integer designating the size of DNS
// Question -> miProxy sends DNS Question via encode()

// nameserver recvs() integer designating size of DNS Header -> nameserver
// recvs() DNS header via decode() -> nameserver recvs() integer designating
// size of DNS Question -> nameserver recvs() DNS Question via decode()

// nameserver sends integer designating size of DNS Header -> nameserver sends
// DNS Header via encode() -> nameserver sends integer designating size of DNS
// Record -> nameserver sends DNS Record via encode()

// miProxy recvs() integer designating size of DNS Header -> miProxy recvs() DNS
// header via decode() -> miProxy recvs() integer designating size of DNS Record
// -> miProxy recvs() DNS Record via decode()

Response response::get_buffer() {
    // Header
    header.AA = htons(query_id);
    header.QR = true;
    header.OPCODE = 0;
    header.AA = true;
    header.TC = false;
    header.RD = false;
    header.RA = false;
    header.Z = '0';
    if (hostname != "video.cse.umich.edu")
        header.RCODE = '3';
    else
        header.RCODE = '0';
    header.QDCOUNT = 0;
    header.ANCOUNT = htons(1);
    header.NSCOUNT = 0;
    header.ARCOUNT = 0;
    // Record
    if (hostname != "video.cse.umich.edu")
        strcpy(record.NAME, "");
    else {
        strcpy(record.NAME, hostname.c_str());
        strcpy(record.RDATA, ip.c_str());
    }
    record.TYPE = htons(1);
    record.CLASS = htons(1);
    record.TTL = 0;
    record.RDLENGTH = htons(ip.length());

    buffer.header = header;
    buffer.record = record;

    return buffer;
}