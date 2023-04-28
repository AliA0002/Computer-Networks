#include "Socket.h"
#include "packet.h"
#include <chrono>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <getopt.h>
#include <ios>
#include <iostream>
#include <memory>
#include <sys/time.h>
#include <vector>
using std::cout;
using std::endl;
using std::make_shared;
using std::ofstream;
using std::shared_ptr;
using std::string;
using std::vector;

void help_string() {
  cout << "Usage: ./wSender <receiver-IP> <receiver-port> <window-size> "
          "<input-file> <log>"
       << endl;
}
struct Log {
  ofstream log;
  Log(string filename) : log(filename) {}
  //   <type> <seqNum> <length> <checksum>
  void write(PacketHeader ph) {
    log << ph.type << " " << ph.seqNum << " " << ph.length << " " << ph.checksum
        << endl;
  }
  ~Log() {
    log.flush();
    log.close();
  }
};

struct args_t {
  string receiver_ip;
  int receiver_port;
  int window_size;
  string input_file;
  Log *log;
  ~args_t() { delete log; }
};

void parse_opts(int argc, char **argv, args_t &args) {
  int option_index = 0, opt = 0;

  // Don't display getopt error messages about options
  opterr = false; // this seems to be declared in global scope

  // use getopt to find command line options
  struct option longOpts[] = {
      {"help", no_argument, nullptr, 'h'},
  };

  while ((opt = getopt_long(argc, argv, "h", longOpts, &option_index)) != -1) {
    switch (opt) {
    case 'h':
      help_string();
      exit(0);
    }
  }
  if (argc - optind != 5) {
    help_string();
    exit(1);
  }
  args.receiver_ip = argv[optind];
  args.receiver_port = atoi(argv[optind + 1]);
  args.window_size = atoi(argv[optind + 2]);
  args.input_file = argv[optind + 3];
  args.log = new Log(argv[optind + 4]);
  return;
}

struct File {
  std::ifstream file;
  std::streamsize size;
  int left;
  int num;
  File(string filename) : file(filename, std::ios::binary) {
    if (!file.is_open()) {
      perror("Error opening file");
      exit(1);
    }
    file.seekg(0, file.end);
    size = file.tellg();
    file.seekg(0, file.beg);

    // std::ifstream tmp(filename, std::ios::binary | std::ios::ate);
    // size = 84629;
    // tmp.close();

    left = size;
    num = 0;
  }
  shared_ptr<Packet> next() {
    int maxdataLen = MTU - sizeof(PacketHeader);
    char buffer[MTU];
    int cur_size = left > maxdataLen ? maxdataLen : left;
    file.read(buffer, cur_size);
    // shared_ptr<Packet> p();
    left -= cur_size;
    num++;
    return make_shared<Packet>(num - 1, cur_size, buffer);
  }
  bool done() { return left == 0; }
  ~File() { file.close(); }
};
size_t first_false(vector<bool> &v) {
  for (size_t i = 0; i < v.size(); i++) {
    if (!v[i]) {
      return i;
    }
  }
  return string::npos;
}

int main(int argc, char **argv) {
  args_t args;
  parse_opts(argc, argv, args);
  // make a socket
  int fd = make_sock(args.receiver_ip.c_str(), args.receiver_port);
  if (fd < 0) {
    perror("Error making socket");
    exit(1);
  }
  socket_raii sr(fd);
  // send a start
  unsigned int start_seq = rand();

  struct sockaddr_in receiver;
  make_sockaddr(&receiver, args.receiver_ip.c_str(), args.receiver_port);

  Packet start(PACKET_START, start_seq);
  bool acked = false;
  do {
    // std::cout << "Send Start" << std::endl;
    if (start.send(fd, &receiver))
      args.log->write(start.getHeader());
    Packet ack(Packet::recv(fd, &receiver));
    if (ack.validate() && ack.getHeader().type == PACKET_ACK &&
        ack.getHeader().seqNum == start_seq) {
      acked = true;
      // std::cout << "got Ack" << std::endl;
    }
    if (ack.getHeader().type != PACKET_ERR) {
      args.log->write(ack.getHeader());
    }
    // std::cout << "no Ack" << std::endl;
  } while (!acked);

  std::deque<shared_ptr<Packet>> *cwnd = new std::deque<shared_ptr<Packet>>();
  File f(args.input_file);
  while (!f.done() || !cwnd->empty()) {
    while (cwnd->size() < args.window_size && !f.done()) {
      cwnd->push_back(f.next());
      // std::cout << "Load: " << cwnd->back()->getHeader().seqNum << std::endl;
    }
    for (shared_ptr<Packet> p : *cwnd) {
      if (p->send(fd, &receiver))
        args.log->write(p->getHeader());
    }
    Packet ack(Packet::recv(fd, &receiver));
    if (ack.validate() && ack.getHeader().type == PACKET_ACK) {
      unsigned int last_ack = ack.getHeader().seqNum;
      for (auto i = cwnd->begin(); i != cwnd->end(); i++) {
        if ((*i)->getHeader().seqNum == last_ack) {
          (*i)->ack();
          break;
        }
      }
      // std::cout << "got Ack " << last_ack << endl;
    }
    if (ack.getHeader().type != PACKET_ERR) {
      args.log->write(ack.getHeader());
    }
    while (!cwnd->empty() && cwnd->front()->isAcked()) {
      // std::cout << "Dump: " << cwnd->front()->getHeader().seqNum <<
      // std::endl;
      cwnd->pop_front();
    }
  }

  Packet end(PACKET_END, start_seq);
  acked = false;
  do {
    if (end.send(fd, &receiver))
      args.log->write(end.getHeader());
    Packet ack(Packet::recv(fd, &receiver));
    if (ack.validate() && ack.getHeader().type == PACKET_ACK &&
        ack.getHeader().seqNum == start_seq) {
      acked = true;
    }
    if (ack.getHeader().type != PACKET_ERR) {
      args.log->write(ack.getHeader());
    }
  } while (!acked);
  delete cwnd;
  // }
  // send a finish
  // recv an ack

  return 0;
}