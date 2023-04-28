#include "main.h"
#include "Socket.h"
#include "params.h"
#include <DNSHeader.h>
#include <DNSQuestion.h>
#include <DNSRecord.h>
#include <cstring>
#include <netinet/in.h>
#include <unordered_map>
#include <vector>

using std::cout;
using std::endl;
using std::string;
using std::unordered_map;

void help_string() {
    cout << "Usage: ./miProxy --nodns <listen-port> <www-ip> <alpha> <log>" << endl;
    cout << "       ./miProxy --dns <listen-port> <dns-ip> <dns-port> "
            "<alpha> <log>"
         << endl;
}

void parse_opts(int argc, char **argv, args_t &args) {
    int option_index = 0, opt = 0;

    // Don't display getopt error messages about options
    opterr = false; // this seems to be declared in global scope

    // use getopt to find command line options
    struct option longOpts[] = {
        {"nodns", no_argument, nullptr, 'n'},
        {"dns", no_argument, nullptr, 'd'},
        {"help", no_argument, nullptr, 'h'},
    };

    bool dns = false;
    bool nodns = false;

    while ((opt = getopt_long(argc, argv, "ndh", longOpts, &option_index)) != -1) {
        switch (opt) {
        case 'n':
            nodns = true;
            break;
        case 'd':
            dns = true;
            break;
        case 'h':
            help_string();
            exit(0);
        }
    }

    if (!(nodns ^ dns)) {
        help_string();
        exit(1);
    }

    if (dns) {
        check_or_fail(argc - optind == 5, "Error: missing or extra arguments");

        int listen_port = atoi(argv[optind]);
        check_or_fail(listen_port > 0 && listen_port < 65536, "Error: Illegal Listen Port number");
        args.listen_port = listen_port;

        string dns_ip = argv[optind + 1];
        check_or_fail(is_valid_ip(dns_ip), "Error: Illegal Web Server IP address");
        args.dns_ip = dns_ip;

        int dns_port = atof(argv[optind + 3]);
        args.dns_port = dns_port;

        int alpha = atof(argv[optind + 3]);
        args.alpha = alpha;

        string logFile = argv[optind + 4];
        args.logfile = logFile;
    } else {
        check_or_fail(argc - optind == 4, "Error: missing or extra arguments");
        int listen_port = atoi(argv[optind]);
        check_or_fail(listen_port > 0 && listen_port < 65536, "Error: Illegal Listen Port number");
        args.listen_port = listen_port;

        string www_ip = argv[optind + 1];
        check_or_fail(is_valid_ip(www_ip), "Error: Illegal Web Server IP address");
        args.web_server_ip = www_ip;

        int alpha = atof(argv[optind + 2]);
        args.alpha = alpha;
        string logFile = argv[optind + 3];
        args.logfile = logFile;
    }
}

// make_dns_request(dns, dns_port, )

// thread
/*
make dns query
find manifest
while connection is open
    start sending bitrate
    move to VBR
*/

struct Session {
    string client_ip;
    string server_ip;
    int server_fd;
};
class BitrateTracker {
  public:
    BitrateTracker(double alpha, std::vector<int> avaliable_bitrates) {
        this->alpha = alpha;
        this->avaliable_bitrates = avaliable_bitrates;
        this->curTput = avaliable_bitrates[0];
    }
    void update(double tput) { this->curTput = alpha * tput + (1 - alpha) * this->curTput; }
    double get_tput() { return this->curTput; }
    int get_bitrate() {
        int i = 0;
        while (i < 4 && avaliable_bitrates[i] * 1.5 <= this->curTput) {
            i++;
        }
        return avaliable_bitrates[i];
    }

  private:
    std::vector<int> avaliable_bitrates;
    double alpha;
    double curTput;
};

struct loc {
    int seg;
    int frag;
    bool valid() { return seg != 0 && frag != 0; }
    string to_string() { return "Seg" + std::to_string(seg) + "-Frag" + std::to_string(frag); }
};
// Seg1-Frag1
loc parse(string s) {
    size_t spos = s.find("Seg") + 3;
    size_t fpos = s.find("Frag") + 4;
    if (spos == string::npos || fpos == string::npos) {
        return loc{0, 0};
    } else {
        size_t send = s.find('-', spos) - spos;
        size_t fend = s.find(' ', fpos) - fpos;
        int seg = atoi(s.substr(spos, send).c_str());
        int frag = atoi(s.substr(fpos, fend).c_str());
        return loc{seg, frag};
    }
}

// string replaceLoc(string originalHeader, loc update){
//     size_t pos = originalHeader.find("Seg");
//     size_t end = originalHeader.find(" ", pos);
//     string newHeader = originalHeader.substr(0, pos) + update.to_string() + originalHeader.substr(end);
//     return newHeader;
// }

int headerEnd(char *buf, int len, int offset = 0) {
    for (int i = offset; i < len; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
            return i;
    return -1;
}

int main(int argc, char **argv) {
    args_t args;
    try {
        // parse_opts(argc, argv, args);
    } catch (std::runtime_error &e) {
        std::cout << e.what() << std::endl;
        return 1;
    }

    // RUN PROXY
    // setup socket to listen for brower connections
    int server_sock = socket_init(8000);
    socket_listen(server_sock, 10);
    Log l(args.logfile);
    fd_set FDs;

    // std::unordered_map<string, int> server_fd;  // server_ip -> server_fd
    std::unordered_map<int, string> connection; // client_fd -> client_ip
    // Session
    std::unordered_map<string, string> dns_cache;       // client_ip -> server_ip
    std::unordered_map<string, BitrateTracker> tracker; // client_ip -> tracker

    while (true) {
        FD_ZERO(&FDs);
        FD_SET(server_sock, &FDs);

        for (auto i = connection.begin(); i != connection.end(); ++i) {
            FD_SET(i->first, &FDs);
        }

        int activity = select(FD_SETSIZE, &FDs, NULL, NULL, NULL);
        if (activity < 0) {
            perror("Error in select");
            return -1;
        }

        // server socket connections - does this need to be in a while?
        if (FD_ISSET(server_sock, &FDs)) {
            // new connection
            int newfd = socket_accept(server_sock);
            if (newfd == -1) {
                perror("Error accepting connection");
                return -1;
            } else {
                string server_ip = ""; // TODO HARDCODE
                string client_ip = get_ip_addr(newfd);
                if (dns_cache.find(client_ip) != dns_cache.end()) {
                    server_ip = dns_cache[client_ip];
                } else if (args.dns) {
                    // dns stuff
                    DNSHeader header = {0 /*TODO change to ID*/, 0, 0 /*standard query?*/, 0,
                                        0 /*TODO is this tc right?*/, 0, 0, 0, 0 /*TODO change rcode*/, 
                                        1 /*TODO is this qdcount right*/, 0 /*TODO change to ancount*/, 0, 0};
                    DNSQuestion question = DNSQuestion();
                    question.QTYPE = 1;
                    question.QCLASS = 1;
                } else {
                }
                dns_cache[client_ip] = server_ip;
                std::vector<int> v = {10, 100, 500, 1000};
                if (tracker.find(client_ip) == tracker.end())
                    tracker.insert({client_ip, BitrateTracker(args.alpha, v)});
                connection[newfd] = client_ip;
            }
        }

        for (auto i = connection.begin(); i != connection.end(); i++) {
            if (FD_ISSET(i->first, &FDs)) {
                socket_raii c(i->first);
                int client_fd = i->first;
                string client_ip = i->second;
                string server_ip = dns_cache[i->second];

                char buf[BUF_SIZE];
                memset(buf, 0, BUF_SIZE);
                int offset = 0;
                int read_len = 0;
                time_t start = time(NULL);
                while (headerEnd(buf, BUF_SIZE, offset) != -1 && read_len > 0) {
                    read_len = socket_recv(client_fd, &buf, BUF_SIZE - offset);
                    offset += read_len;
                }
                if (read_len < 0) {
                    perror("Error reading from socket");
                    return -1;
                } else if (read_len == 0) { // we had a close so no use sending data back
                    socket_raii c(client_fd);
                    // socket_raii w(webserver_fd); - need to do lifecycle cleanup on this later
                    i = connection.erase(i);
                    continue;
                } else {
                    string req = string(buf); // request Header
                    loc requested = parse(req);
                    size_t manPos = req.find(".f4m");
                    string modified_req = req;
                    if (requested.valid()) { // asked for a video - change the bitrate + seg

                    } else if (manPos != string::npos) { // asked for a manifest
                        // manifest stuff (nolist + parse original)
                    } else {
                        // something else?
                    }
                    socket_raii webserver(make_sock(server_ip.c_str(), 80)); // socket for the request
                    int res = socket_send_all(webserver.fd, modified_req.c_str(), modified_req.length());
                    if (res < 0) {
                        perror("Error sending to socket");
                        continue;
                    }
                    // At this point, my only use, is to read what the server sends back, and forward it?
                    // TODO - I may have to reform the header when i send it back to the client
                    memset(buf, 0, BUF_SIZE);
                    read_len = 0;
                    offset = 0;
                    while (headerEnd(buf, BUF_SIZE, offset) != -1 && read_len > 0) { // just want the header for now
                        read_len = socket_recv(webserver.fd, buf + offset, BUF_SIZE - offset);
                        offset += read_len;
                    }
                    int hend = headerEnd(buf, BUF_SIZE);
                    string respHeader(buf);
                    respHeader = respHeader.substr(0, hend);
                    // parse the header to get the content length
                    size_t clenPos = respHeader.find("Content-Length: ");
                    size_t clenEnd = respHeader.find("\r", clenPos);
                    string clenStr = respHeader.substr(clenPos + 16, clenEnd - clenPos - 16);
                    int clen = atoi(clenStr.c_str());
                    // read and forward the rest of the data
                    socket_send(client_fd, respHeader);
                    socket_send_all(client_fd, buf + hend, offset - hend);
                    int r, s;
                    while (clen > 0 && r > 0 && s > 0) {
                        r = socket_recv(webserver.fd, buf, BUF_SIZE);
                        s = socket_send_all(client_fd, buf, r);
                        clen -= r;
                    }
                    time_t end = time(NULL);
                    // log_entry.duration = chunk_end - chunk_start;
                    // log_entry.tput = bytes_sent / 125;  // Report tput as Kbps
                    // log_entry.avg_tput = bt.get_tput(); // Should avg_tput be before or after we update?
                    int brate = tracker.at(client_ip).get_bitrate();
                    double tput = ((atoi(clenStr.c_str()) + respHeader.length()) / 125.) / (end - start);
                    tracker.at(client_ip).update(tput);
                    l.log({client_ip, requested.to_string(), server_ip, end - start, tput,
                           tracker.at(client_ip).get_tput(), brate});
                    i = connection.erase(i);
                }
            }
        }

        // std::thread conn_thread(&handle_connection, confd, args, &l);
        // conn_thread.detach();
    }
}