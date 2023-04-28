#include "packet.h"
#include <crc32.h>

using std::string;
using std::vector;
using std::chrono::nanoseconds;
using std::chrono::steady_clock;

Packet::Packet(unsigned int seqNum, unsigned int size, void *buffer)
    : type(PACKET_DATA), seqNum(seqNum), size(size)
{
  assert(size + sizeof(PacketHeader) <= MTU);
  this->data = new char[size];
  memcpy(this->data, buffer, size);
  this->checksum = crc32(this->data, this->size);
}

Packet::Packet(unsigned int type, unsigned int seqNum)
    : type(type), seqNum(seqNum)
{
  assert(this->type != PACKET_DATA);
  this->size = 0;
  this->data = nullptr;
  this->checksum = 0;
}
Packet::~Packet()
{
  if (this->data)
    delete[] this->data;
}

PacketHeader Packet::getHeader()
{
  PacketHeader ph;
  ph.type = this->type;
  ph.seqNum = this->seqNum;
  ph.length = this->size;
  ph.checksum = this->checksum;
  return ph;
}

bool Packet::send(int fd)
{
  if (this->acked)
    return false;
  auto now = steady_clock::now();
  auto duration = std::chrono::duration_cast<nanoseconds>(now - this->time).count();
  if (duration < PACKET_TIMEOUT)
    return false;
  PacketHeader ph = this->getHeader();
  char packBuf[MTU];
  memcpy(packBuf, &ph, sizeof(PacketHeader));
  memcpy(packBuf + sizeof(PacketHeader), this->data, this->size);
  int n = socket_send(fd, packBuf, sizeof(PacketHeader) + this->size);
  if (n < 0)
  {
    perror("Error sending packet");
    return false;
  }
  this->time = steady_clock::now(); // only update on success
  return true;
}

Packet Packet::recv(int fd)
{
  char packBuf[MTU];
  int n = socket_recv(fd, packBuf, MTU);
  if (n < 0)
  {
    perror("Error receiving packet");
    // will fail validation beats needing to use pointers
    return Packet(PACKET_ERR, 0);
  }
  return Packet(packBuf);
}

Packet::Packet(char *buf)
{
  PacketHeader ph;
  memcpy(&ph, buf, sizeof(PacketHeader));
  Packet packet(ph.type, ph.seqNum);
  this->type = ph.type;
  this->seqNum = ph.seqNum;
  this->size = ph.length;
  this->checksum = ph.checksum;
  if (this->data)
    delete[] this->data;
  this->data = new char[this->size];
  memcpy(this->data, buf + sizeof(PacketHeader), this->size);
}

bool Packet::validate()
{
  return this->type != PACKET_ERR &&
         this->checksum == crc32(this->data, this->size);
}

void Packet::ack() { this->acked = true; }
bool Packet::isAcked() { return this->acked; }