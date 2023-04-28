#ifndef _PACKET_H_
#define _PACKET_H_
#include "Socket.h"
#include <PacketHeader.h>
#include <chrono>
#include <vector>
#include <cassert>
using std::chrono::nanoseconds;
using std::chrono::steady_clock;
using std::chrono::time_point;
#define BUFFER_SIZE 1024
#define MTU 1500U
#define PACKET_START 0U
#define PACKET_END 1U
#define PACKET_DATA 2U
#define PACKET_ACK 3U
#define PACKET_ERR 4U
#define PACKET_TIMEOUT 500000000UL // 500ms*1000µs*1000ns

// unsigned int type;     // 0: START; 1: END; 2: DATA; 3: ACK
// unsigned int seqNum;   // Described below
// unsigned int length;   // Length of data segment; 0 for ACK packets
// unsigned int checksum; // 32-bit CRC

class Packet
{
public:
  Packet(unsigned int type, unsigned int seqNum);
  Packet(unsigned int seqNum, unsigned int size, void *buffer);
  ~Packet();
  PacketHeader getHeader();
  bool send(int fd);
  static Packet recv(int fd);
  bool validate();
  void ack();
  bool isAcked();

private:
  Packet(char *buf);
  unsigned int type;
  unsigned int seqNum;
  unsigned int size; // size of data segment
  unsigned int checksum;
  char *data;
  time_point<steady_clock> time;
  bool acked;
};

#endif