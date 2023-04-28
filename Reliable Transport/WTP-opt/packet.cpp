#include "packet.h"
#include "crc32.h"
#include <sys/socket.h>

using std::string;
using std::vector;
using std::chrono::nanoseconds;
using std::chrono::steady_clock;

Packet::Packet(unsigned int seqNum, unsigned int size, void *buffer)
    : type(PACKET_DATA), seqNum(seqNum), size(size) {
  assert(size + sizeof(PacketHeader) <= MTU);
  memcpy(this->data, buffer, size);
  this->checksum = crc32(this->data, this->size);
  this->acked = false;
}

Packet::Packet(unsigned int type, unsigned int seqNum)
    : type(type), seqNum(seqNum) {
  // assert(this->type != PACKET_DATA);
  this->size = 0;
  this->checksum = 0;
  this->acked = false;
}
Packet::~Packet() {}

PacketHeader Packet::getHeader() {
  PacketHeader ph;
  ph.type = this->type;
  ph.seqNum = this->seqNum;
  ph.length = this->size;
  ph.checksum = this->checksum;
  return ph;
}

bool Packet::send(int fd, struct sockaddr_in *addr) {
  if (this->acked) {
    return false;
  }
  auto now = steady_clock::now();
  auto duration =
      std::chrono::duration_cast<nanoseconds>(now - this->time).count();
  if (duration < PACKET_TIMEOUT) {
    return false;
  }
  PacketHeader ph = this->getHeader();
  char packBuf[MTU];
  memcpy(packBuf, &ph, sizeof(PacketHeader));
  memcpy(packBuf + sizeof(PacketHeader), this->data, this->size);
  int n = sendto(fd, packBuf, sizeof(PacketHeader) + this->size, 0,
                 (struct sockaddr *)addr, sizeof(struct sockaddr_in));
  if (n < 0) {
    perror("Error sending packet");
    return false;
  }
  this->time = steady_clock::now(); // only update on success
  return true;
}

Packet Packet::recv(int fd, struct sockaddr_in *addr) {
  char packBuf[MTU];
  socklen_t addrLen = sizeof(struct sockaddr_in);
  unsigned int rand = random();
  int n = recvfrom(fd, packBuf, MTU, 0, (struct sockaddr *)addr, &addrLen);
  if (n < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK)
      perror("Error receiving packet");
    // will fail validation beats needing to use pointers
    return Packet(PACKET_ERR, 0);
  }
  return Packet(packBuf);
}

Packet::Packet(char *buf) {
  PacketHeader ph;
  memcpy(&ph, buf, sizeof(PacketHeader));
  Packet packet(ph.type, ph.seqNum);
  this->type = ph.type;
  this->seqNum = ph.seqNum;
  this->size = ph.length;
  this->checksum = ph.checksum;
  memcpy(this->data, buf + sizeof(PacketHeader), this->size);
}

bool Packet::validate() {
  return this->type != PACKET_ERR &&
         this->checksum == crc32(this->data, this->size);
}

void Packet::ack() { this->acked = true; }
bool Packet::isAcked() { return this->acked; }

int Packet::getData(void *buffer) {
  memcpy(buffer, this->data, this->size);
  return this->size;
}