#include "Socket.h"
#include "packet.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <sys/time.h>
#include <vector>
using std::cout;
using std::endl;
using std::ofstream;
using std::string;
using std::vector;
using std::chrono::duration_cast;
using std::chrono::nanoseconds;
using std::chrono::steady_clock;

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
    log.flush();
  }
  ~Log() { log.close(); }
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

// read file
vector<Packet> breakFile(string filename) {
  vector<Packet> packets;
  std::ifstream file("myfile", std::ios::binary | std::ios::ate);
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  int left = size;
  int maxdataLen = MTU - sizeof(PacketHeader);
  char buffer[BUFFER_SIZE];
  int num = 0;
  while (left > 0) {
    int cur_size = left > maxdataLen ? maxdataLen : left;
    file.read(buffer, cur_size);
    packets.push_back(Packet(num, cur_size, buffer));
    left -= cur_size;
    num++;
  }
  return packets;
}
// struct timeval {
//   time_t tv_sec;    // Number of whole seconds of elapsed time
//   long int tv_usec; // Number of microseconds of rest of elapsed time minus
//                     // tv_sec. Always less than one million
// };

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
  int start_seq = rand();
  fd_set readfds;
  int act;
  struct timeval timeout = {0, PACKET_TIMEOUT / 1000};
  {
    Packet start(PACKET_START, start_seq);
    bool acked = false;
    do {
      start.send(fd);
      FD_ZERO(&readfds);
      FD_SET(fd, &readfds);
      act = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);
      if (act == 0) {
        continue;
      } else if (act < 0) {
        perror("select");
        exit(1);
      }
      // recv an ack
      if (FD_ISSET(fd, &readfds)) {
        Packet ack(Packet::recv(fd));
        if (ack.validate() && ack.getHeader().type == PACKET_ACK &&
            ack.getHeader().seqNum == start_seq) {
          acked = true;
        }
      }
    } while (!acked);
  }
  {
    // read file
    vector<Packet> packets = breakFile(args.input_file);
    // send packets
    int base_ack = 0;
    while (base_ack < packets.size()) {
      for (int i = base_ack; i < base_ack + args.window_size; i++) {
        if (packets[i].send(fd))
          args.log->write(packets[i].getHeader());
      }
      FD_ZERO(&readfds);
      FD_SET(fd, &readfds);
      act = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);
      if (act == 0) {
        continue;
      } else if (act < 0) {
        perror("select");
        exit(1);
      }
      // recv an ack
      if (FD_ISSET(fd, &readfds)) {
        Packet ack(Packet::recv(fd));
        if (ack.validate() && ack.getHeader().type == PACKET_ACK) {
          packets[ack.getHeader().seqNum].ack();
        }
      }
      // May need to rethink
      for (auto i = base_ack; i < packets.size(); i++) {
        if (packets[i].isAcked())
          base_ack++;
        else // first one that hasnt been acked
          break;
      }
    }
  }
  {
    // finish
    Packet end(PACKET_END, start_seq);
    bool acked = false;
    do {
      end.send(fd);
      FD_ZERO(&readfds);
      FD_SET(fd, &readfds);
      act = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);
      if (act == 0) {
        continue;
      } else if (act < 0) {
        perror("select");
        exit(1);
      }
      // recv an ack
      if (FD_ISSET(fd, &readfds)) {
        Packet ack(Packet::recv(fd));
        if (ack.validate() && ack.getHeader().type == PACKET_ACK &&
            ack.getHeader().seqNum == start_seq) {
          acked = true;
        }
      }
    } while (!acked);
  }
  // send a finish
  // recv an ack

  return 0;
}