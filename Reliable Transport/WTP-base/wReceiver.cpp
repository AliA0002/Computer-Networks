#include "Socket.h"
#include "packet.h"
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <getopt.h>
#include <ios>
#include <iostream>
#include <memory>
#include <string>
#include <sys/time.h>
#include <vector>
using std::cout;
using std::deque;
using std::endl;
using std::make_shared;
using std::ofstream;
using std::shared_ptr;
using std::string;
using std::to_string;
using std::vector;

void help_string() {
  cout << "Usage: ./wReceiver <port-num> <window-size> <output-dir> <log>"
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
  int port_num;
  int window_size;
  string output_dir;
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
  if (argc - optind != 4) {
    help_string();
    exit(1);
  }
  args.port_num = atoi(argv[optind]);
  args.window_size = atoi(argv[optind + 1]);
  args.output_dir = argv[optind + 2];
  args.log = new Log(argv[optind + 3]);
  return;
}
//  Seq num is the chunk num
void dump_data(shared_ptr<Packet> packet, args_t &args, int connection) {
  ofstream out(args.output_dir + "/FILE-" + to_string(connection) + ".out",
               std::ios::binary | std::ios::app);
  //   cout << "Writing chunk: " << packet->getHeader().seqNum << endl;
  char buf[MTU];
  int len = packet->getData(buf);
  out.write(buf, len);
  out.close();
}
struct FF {
  size_t last;
  FF() : last(0) {}
  size_t operator()(vector<bool> &v) {
    for (size_t i = last; i < v.size(); i++) {
      if (!v[i]) {
        last = i;
        return i;
      }
    }
    return string::npos;
  }
};

int main(int argc, char **argv) {
  args_t args;
  parse_opts(argc, argv, args);

  struct sockaddr_in sender;
  makeSockAddr(&sender, args.port_num);

  int fd = socket_init(args.port_num);
  if (fd < 0) {
    perror("Error making socket");
    exit(1);
  }
  socket_raii sr(fd);
  int conn_count = 0;
  while (true) {
    vector<bool> receivedChunks(8 * 1024, false);
    FF first_false;
    // recv a start
    int initial_seq = -1;
    {
      bool got_start = false;
      while (!got_start) {
        Packet start(Packet::recv(fd, &sender));
        if (start.validate() && start.getHeader().type == PACKET_START) {
          //   cout << "Got Start" << endl;
          initial_seq = start.getHeader().seqNum;
          got_start = true;
        } else if (start.validate() && start.getHeader().type == PACKET_END) {
          Packet ack(PACKET_ACK, start.getHeader().seqNum);
          if (ack.send(fd, &sender))
            args.log->write(ack.getHeader());
          //   cout << "Got end" << endl;
        }
      }
    }
    shared_ptr<Packet> maybe_data;
    ofstream out(args.output_dir + "/FILE-" + to_string(conn_count) + ".out",
                 std::ios::binary);
    out.close();
    deque<shared_ptr<Packet>> buffer =
        deque<shared_ptr<Packet>>(args.window_size, nullptr);
    {
      Packet ack(PACKET_ACK, initial_seq);
      if (ack.send(fd, &sender))
        args.log->write(ack.getHeader());

      bool got_data = false;
      while (!got_data) {
        maybe_data = make_shared<Packet>(Packet::recv(fd, &sender));
        if (maybe_data->validate() &&
            maybe_data->getHeader().type == PACKET_START) {
          if (ack.send(fd, &sender))
            args.log->write(ack.getHeader());
          //   cout << "Got Start Again" << endl;
        } else if (maybe_data->validate() &&
                   maybe_data->getHeader().type == PACKET_DATA) {
          got_data = true;
          //   cout << "Got Data" << endl;

        } else if (maybe_data->validate() &&
                   maybe_data->getHeader().type == PACKET_ERR) {
          //   cout << "Got err" << endl;
        }
        if (maybe_data->getHeader().type != PACKET_ERR) {
          args.log->write(maybe_data->getHeader());
        }
      }
    }
    // maybe data is now the first data packet
    if (maybe_data->getHeader().seqNum <
        first_false(receivedChunks) + args.window_size) {
      if (maybe_data->getHeader().seqNum == first_false(receivedChunks)) {
        dump_data(maybe_data, args, conn_count);
        receivedChunks[maybe_data->getHeader().seqNum] = true;
        buffer.pop_front();
        buffer.push_back(nullptr);
      } else {
        buffer[maybe_data->getHeader().seqNum - first_false(receivedChunks)] =
            maybe_data;
        // receivedChunks[maybe_data->getHeader().seqNum] = true;
      }
      while (buffer.front() != nullptr) {
        dump_data(buffer.front(), args, conn_count);
        receivedChunks[buffer.front()->getHeader().seqNum] = true;
        buffer.pop_front();
        buffer.push_back(nullptr);
      }
    }
    shared_ptr<Packet> endPack;
    {
      Packet ack(PACKET_ACK, first_false(receivedChunks));
      if (ack.send(fd, &sender))
        args.log->write(ack.getHeader());
      bool recvd_end = false;
      while (!recvd_end) {
        shared_ptr<Packet> next =
            make_shared<Packet>(Packet::recv(fd, &sender));
        if (next->validate() && next->getHeader().type == PACKET_DATA) {
          //   cout << "Got Data " << next->getHeader().seqNum << endl;
          if (!receivedChunks[next->getHeader().seqNum] &&
              next->getHeader().seqNum <
                  first_false(receivedChunks) + args.window_size) {

            if (next->getHeader().seqNum == first_false(receivedChunks)) {
              dump_data(next, args, conn_count);
              receivedChunks[next->getHeader().seqNum] = true;
              buffer.pop_front();
              buffer.push_back(nullptr);
            } else {
              buffer[next->getHeader().seqNum - first_false(receivedChunks)] =
                  next;
              // receivedChunks[next->getHeader().seqNum] = true;
            }
            while (buffer.front() != nullptr) {
              dump_data(buffer.front(), args, conn_count);
              receivedChunks[buffer.front()->getHeader().seqNum] = true;
              buffer.pop_front();
              buffer.push_back(nullptr);
            }
          } // else = duplicate or out of range, but ack it anyway
          Packet ack(PACKET_ACK, first_false(receivedChunks));
          //   cout << "Acking " << ack.getHeader().seqNum << endl;
          if (ack.send(fd, &sender))
            args.log->write(ack.getHeader());
        } else if (next->validate() && next->getHeader().type == PACKET_END) {
          recvd_end = true;
          endPack = next;
          //   cout << "Got End" << endl;
        } else if (next->validate() && next->getHeader().type == PACKET_ERR) {
          //   cout << "Got err" << endl;
        }
      }
    }
    // ack the end - how do i handle a drop of this?

    time_point<steady_clock> last_msg = steady_clock::now();

    // wait for 2 timeouts before exiting
    Packet ack(PACKET_ACK, endPack->getHeader().seqNum);
    while (
        std::chrono::duration_cast<nanoseconds>(last_msg - steady_clock::now())
            .count() < 2 * PACKET_TIMEOUT) {
      if (ack.send(fd, &sender))
        args.log->write(ack.getHeader());
      Packet reend(Packet::recv(fd, &sender));
      if (reend.validate() && reend.getHeader().type == PACKET_END) {
        // cout << "Got End Again" << endl;
      } else if (reend.validate() && reend.getHeader().type == PACKET_ERR) {
        // cout << "Got d" << endl;
      }
    }
    conn_count++;
  }
  return 0;
}