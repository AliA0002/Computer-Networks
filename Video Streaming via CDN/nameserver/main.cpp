
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
#include <ostream>
#include <queue>
#include <stdexcept>
#include <sys/time.h> //FD_SET, FD_ISSET, FD_ZERO, FD_SETSIZE macros
#include <vector>

using std::cout;
using std::deque;
using std::endl;
using std::istream;
using std::ofstream;
using std::string;
using std::vector;

// TODO: look into the encoding/decoding functions
// TODO: Testing!
enum LBMode : bool { GEO, RR };
class Resolver {
  public:
    virtual string resolve(string client_ip) = 0;
    virtual ~Resolver() {}
};

void help_string() {
    cout << "Usage: ./nameserver --geo <port> <serverfile> <log>" << endl;
    cout << "       ./nameserver --rr <port> <serverfile> <log>" << endl;
}

class Log {
  private:
    ofstream log;

  public:
    Log(string filename) : log(filename) {}
    void write(string client, string query, string response) {
        log << client << " " << query << " " << response << endl;
    }
    void flush_log() { log.flush(); }
    ~Log() { log.close(); }
};

class RRResolver : public Resolver {
  private:
    deque<string> servers;

  public:
    RRResolver(string filename) {
        ifstream serverfile(filename);
        string server;
        while (serverfile >> server) {
            servers.push_back(server);
        }
    }
    string resolve(string client_ip) override {
        cout << "RR RESOLVE\n"; // REMOVE
        string server = servers.front();
        servers.pop_front();
        servers.push_back(server);
        cout << "SERVER: " << server << endl;
        return server;
    }
};

class GeoResolver : public Resolver {
  private:
    enum NodeType : char { CLIENT, SWITCH, SERVER };
    struct node {
        string ip;
        NodeType type;
    };
    friend istream &operator>>(istream &is, GeoResolver::NodeType &type);
    vector<node> nodes;
    vector<vector<int>> links;

    struct sc_item {
        int curr_node;
        int curr_dist;
        sc_item() : curr_node(0), curr_dist(0) {}
        sc_item(int node, int dist) : curr_node(node), curr_dist(dist) {}
        sc_item(int node) : curr_node(node), curr_dist(0) {}
    };
    struct sci_comp {
        bool operator()(const sc_item &a, const sc_item &b) { return a.curr_dist > b.curr_dist; }
    };

  public:
    GeoResolver(string filename) {
        ifstream serverfile(filename);
        string js;
        int ji;
        int num_nodes, num_links;
        serverfile >> js >> num_nodes;
        nodes.reserve(num_nodes);
        links.resize(num_nodes, vector<int>(num_nodes, std::numeric_limits<int>::max()));
        for (int i = 0; i < num_nodes; i++) {
            node n;
            serverfile >> ji >> n.type >> n.ip;
            nodes.push_back(n);
        }
        serverfile >> js >> num_links;
        for (int i = 0; i < num_links; i++) {
            int from, to, weight;
            serverfile >> from >> to >> weight;
            links[from][to] = weight;
            links[to][from] = weight;
        }
    }
    string resolve(string client_ip) override {
        std::priority_queue<sc_item, vector<sc_item>, sci_comp> search_cont;
        for (int i = 0; i < nodes.size(); i++) {
            if (nodes[i].type == CLIENT && nodes[i].ip == client_ip) {
                search_cont.push(sc_item(i));
                break;
            }
        }
        if (search_cont.empty())
            throw std::runtime_error("Client not found");

        while (!search_cont.empty()) {
            sc_item next = search_cont.top();
            int node = next.curr_node;
            int dist = next.curr_dist;
            search_cont.pop();
            if (nodes[node].type == SERVER) {
                cout << "RR RESOLVE\n";                       // REMOVE
                cout << "SERVER: " << nodes[node].ip << endl; // REMOVE
                return nodes[node].ip;
            } else {
                for (int i = 0; i < nodes.size(); i++) {
                    if (links[node][i] != std::numeric_limits<int>::max()) {
                        search_cont.push(sc_item(i, dist + links[node][i]));
                    }
                }
            }
        }
        throw std::runtime_error("No solution found");
    }
};

istream &operator>>(istream &is, GeoResolver::NodeType &type) {
    string s;
    is >> s;
    if (s == "CLIENT") {
        type = GeoResolver::NodeType::CLIENT;
    } else if (s == "SWITCH") {
        type = GeoResolver::NodeType::SWITCH;
    } else if (s == "SERVER") {
        type = GeoResolver::NodeType::SERVER;
    } else {
        throw std::runtime_error("Invalid node type");
    }
    return is;
}

struct args_t {
    Resolver *r;
    Log *log;
    int port;
    LBMode mode;

    ~args_t() {
        // delete r;
        // delete log;
    }
};

int handle_connection(int fd, args_t *args) {
    socket_raii sr(fd);
    // what do i need to do
    char buf[BUF_SIZE];
    memset(buf, 0, BUF_SIZE);
    cout << "TEST 7\n" << endl; // REMOVE
    int len;
    int n = socket_recv_all(fd, &len, sizeof(len));
    if (n == 0)
        return 0;
    if (n == -1)
        return -1;
    len = ntohl(len);
    cout << "REQUEST (buf1): " << string(buf) << endl; // REMOVE

    memset(buf, 0, BUF_SIZE);
    n = socket_recv_all(fd, buf, len);
    cout << "REQUEST (buf2): " << string(buf) << endl; // REMOVE
    if (n == -1)
        return -1;
    string header_string = string(buf, buf + len);
    DNSHeader header = DNSHeader::decode(buf);

    memset(buf, 0, BUF_SIZE);
    n = socket_recv_all(fd, &len, sizeof(len));
    if (n == -1)
        return -1;
    len = ntohl(len);

    memset(buf, 0, BUF_SIZE);
    n = socket_recv_all(fd, buf, len);
    if (n == -1)
        return -1;
    string question_string = string(buf, buf + len);
    DNSQuestion question = DNSQuestion::decode(buf);

    string client_ip = get_ip_addr(fd);
    string response;
    // do some processing here
    if (strcmp(question.QNAME, "video.cse.umich.edu") == 0) {
        cout << "CALL RESOLVE: " << client_ip << endl;
        response = args->r->resolve(client_ip);
    }
    // response time
    DNSHeader resp_header;
    resp_header.ID = header.ID;
    resp_header.QR = 1;
    resp_header.OPCODE = 0;
    resp_header.AA = 1;
    resp_header.TC = 0;
    resp_header.RD = 0;
    resp_header.RA = 0;
    resp_header.Z = 0;
    resp_header.RCODE = (strcmp(question.QNAME, "video.cse.umich.edu") == 0) ? 0 : 3; // 3 on fail
    resp_header.QDCOUNT = 1;
    resp_header.ANCOUNT = 1;
    resp_header.NSCOUNT = 0;
    resp_header.ARCOUNT = 0;

    DNSRecord resp_record;
    memcpy(resp_record.NAME, question.QNAME, 100); // name we are sending back
    strcpy(resp_record.RDATA, response.c_str());   // resp data
    resp_record.TYPE = 1;
    resp_record.CLASS = 1;
    resp_record.TTL = 0;
    resp_record.RDLENGTH = strlen(resp_record.RDATA);

    string resp_header_string = DNSHeader::encode(resp_header);
    string resp_record_string = DNSRecord::encode(resp_record);

    int resp_header_len = htonl(resp_header_string.length());
    int resp_record_len = htonl(resp_record_string.length());

    char *head = buf;
    socket_send(fd, &resp_header_len, sizeof(resp_header_len));
    socket_send(fd, resp_header_string.c_str(), resp_header_string.length());
    socket_send(fd, &resp_record_len, sizeof(resp_record_len));
    socket_send(fd, resp_record_string.c_str(), resp_record_string.length());

    // log
    args->log->write(client_ip, question.QNAME, response);
    args->log->flush_log();
    cout << "POST LOG\n"; // REMOVE
    return 0;
}
void parse_opts(int argc, char **argv, args_t &args) {
    int option_index = 0, opt = 0;

    // Don't display getopt error messages about options
    opterr = false; // this seems to be declared in global scope

    // use getopt to find command line options
    struct option longOpts[] = {
        {"geo", no_argument, nullptr, 'g'},
        {"rr", no_argument, nullptr, 'r'},
    };

    bool geo = false;
    bool rr = false;

    while ((opt = getopt_long(argc, argv, "ndh", longOpts, &option_index)) != -1) {
        switch (opt) {
        case 'g':
            geo = true;
            break;
        case 'r':
            rr = true;
            break;
        case 'h':
            help_string();
            exit(0);
        }
    }

    if (!(geo ^ rr)) {
        help_string();
        exit(1);
    }

    if (geo) {
        args.mode = GEO;
    } else if (rr) {
        args.mode = RR;
    }

    check_or_fail(argc - optind == 3, "Error: missing or extra arguments");

    int port = atoi(argv[optind]);
    check_or_fail(port > 0 && port < 65536, "Error: Illegal Port number");
    args.port = port;

    string server_file = argv[optind + 1];
    if (args.mode == RR)
        args.r = new RRResolver(server_file);
    else if (args.mode == GEO)
        args.r = new GeoResolver(server_file);
    else
        check_or_fail(false, "Unknown mode");

    string logFile = argv[optind + 2];
    args.log = new Log(logFile);
}

int main(int argc, char **argv) {
    args_t args;
    parse_opts(argc, argv, args);

    int sockfd = socket_init(args.port);
    if (sockfd == -1) {
        return -1;
    }

    args.port = socket_getPort(sockfd);
    // (4) Begin listening for incoming connections.
    listen(sockfd, 10);

    fd_set readfds;
    vector<int> fds;
    // (5) Serve incoming connections one by one forever.
    while (true) {
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        for (int fd : fds) {
            FD_SET(fd, &readfds);
        }
        if (select(FD_SETSIZE, &readfds, NULL, NULL, NULL) == -1) {
            perror("Error in select");
            return -1;
        }
        if (FD_ISSET(sockfd, &readfds)) {
            int confd = socket_accept(sockfd);
            if (confd == -1) {
                perror("Error accepting connection");
                return -1;
            }
            fds.push_back(confd);
        }
        for (int i = 0; i < fds.size(); i++) {
            if (FD_ISSET(fds[i], &readfds)) {
                handle_connection(fds[i], &args);
                // std::thread worker(handle_request, fds[i], &args, &state);
                // worker.detach();
                fds.erase(fds.begin() + i);
                i--;
            }
        }
        // std::thread worker(handle_request, confd, &args, &state);
        // worker.detach();
    }
}
