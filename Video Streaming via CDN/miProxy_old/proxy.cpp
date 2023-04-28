#if FALSE

#include "proxy.h"
#include "Socket.h"
#include <cctype>
#include <sstream>

using std::cout;
using std::endl;
using std::stringstream;
using std::vector;

static const int avaliable_bitrates[] = {10, 100, 500, 1000};

class Bitrate_Tracker {
  public:
    Bitrate_Tracker(double alpha) {
        this->alpha = alpha;
        this->curTput = avaliable_bitrates[0];
    }
    void update(double tput) { this->curTput = alpha * tput + (1 - alpha) * this->curTput; }
    int get_tput() { return this->curTput; }
    int get_bitrate() {
        int i = 0;
        while (i < 4 && avaliable_bitrates[i] * 1.5 <= this->curTput) {
            i++;
        }
        return avaliable_bitrates[i];
    }

  private:
    double alpha;
    double curTput;
};

int connect_to_server(string ip, int port);
void pull_chunk_and_forward(int client_fd, int server_fd, vector<string> path, Log *l, Bitrate_Tracker &bt);
vector<string> split(string s, char delim = '/');
bool read_http_header(int client_fd, char *request, int buffer_size);

vector<string> split(string s, char delim) {
    vector<string> ret;
    stringstream ss(s.substr(1)); // need to prune off the leading '/'
    string item;
    while (getline(ss, item, delim)) {
        ret.push_back(item);
    }
    return ret;
}

bool read_http_header(int client_fd, char *request, int buffer_size) {
    int offset = 0;
    bool request_complete = false;

    while (!request_complete && offset < buffer_size) {
        int bytes_read = socket_recv(client_fd, &request + offset, buffer_size - offset);
        for (int i = offset; i < offset + bytes_read - 3; i++) {
            if (request[i] == '\r' && request[i + 1] == '\n' && request[i + 2] == '\r' && request[i + 3] == '\n') {
                request_complete = true;
                break; // break out of for loop
            }
        }
        offset += bytes_read;
        if (bytes_read == 0) // socket borked
            return false;
    }
    return offset < buffer_size; // false can also be request was too long
}

int connect_to_server(string ip, int port) { return make_sock(ip.c_str(), port); }

stringstream join(vector<string> path, string delim = "/") {
    stringstream out("");
    for (string s : path) {
        out << delim << s;
    }
    return out;
}

struct chunk {
    int bitrate;
    int seg;
    int frag;
};

chunk parse(string s) {
    chunk c;
    string t;
    int i = 0;
    for (; i < s.length() && std::isdigit(s[i]); i++)
        t += s[i];
    c.bitrate = atoi(t.c_str());
    t = "";
    // for (;i < s.length() && !std::isdigit(s[i]); i++)
    //     i++;
    i += 3;
    for (; i < s.length() && std::isdigit(s[i]); i++)
        t += s[i];
    c.seg = atoi(t.c_str());
    t = "";
    // for (;i < s.length() && !std::isdigit(s[i]); i++)
    //     i++;
    i += 5;
    for (; i < s.length() && std::isdigit(s[i]); i++)
        t += s[i];
    c.frag = atoi(t.c_str());
    return c;
}
string encode(chunk c) {
    stringstream ss("");
    ss << c.bitrate << "Seg" << c.seg << "-Frag" << c.frag;
    return ss.str();
}
// single chunk, by the time next one happens we should be good?
void pull_chunk_and_forward(int client_fd, int server_fd, vector<string> path, Log *l, Bitrate_Tracker &bt) {
    Log::entry log_entry;
    char buf[8 * 1024];
    int current_bitrate = bt.get_bitrate();
    log_entry.bitrate = current_bitrate;
    // need to modify the path out
    chunk c = parse(path[path.size() - 1]);
    c.bitrate = current_bitrate;
    path[path.size() - 1] = encode(c);
    log_entry.chunkname = join(path).str();
    stringstream http_request("");
    http_request << "GET " << join(path).str() << " HTTP/1.1\r\n"
                 << "Host: localhost\r\n" // maybe change this?
                 << "User-Agent: Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:59.0) Gecko/20100101 Firefox/59.0\r\n"
                 << "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                 << "Accept-Language: en-US,en;q=0.5\r\n"
                 << "Accept-Encoding: gzip, deflate\r\n"
                 << "Connection: keep-alive\r\n"
                 << "Upgrade-Insecure-Requests: 1"
                 << "\r\n\r\n";

    socket_send(server_fd, http_request.str()); // is the added on '\0' going to cause problems?

    // server is now sending data
    int server_len, client_len, client_offset;
    size_t bytes_sent = 0;
    time_t chunk_start = time(nullptr);
    do {
        server_len = socket_recv(server_fd, &buf, 8 * 1024);
        client_offset = 0;
        do {
            client_len = socket_send(client_fd, buf + client_offset, server_len - client_offset);
            client_offset += client_len;
            bytes_sent += client_len;
        } while (client_offset < server_len);
    } while (server_len > 0);
    time_t chunk_end = time(nullptr);
    // log here
    // TODO: add client_ip and server_ip to log_entry
    log_entry.duration = chunk_end - chunk_start;
    log_entry.tput = bytes_sent / 125;                          // Report tput as Kbps
    log_entry.avg_tput = bt.get_tput();                         // Should avg_tput be before or after we update?
    bt.update((bytes_sent / 125.) / (chunk_end - chunk_start)); // Is the 125 (kbit) correct?
}

void handle_connection(int client_fd, const args_t args, Log *l) {
    socket_raii sr(client_fd);

    // TODO: get request from client
    char request[8 * 1024]; // 8 kb

    bool functional = read_http_header(client_fd, request, 8 * 1024);
    if (!functional)
        return;
    cout << request << endl;

    string path;
    for (int i = 0; i < strlen(request); i++) {
        if (request[i] == 'G' && request[i + 1] == 'E' && request[i + 2] == 'T') {
            char *path_ptr = strtok(request + i + 3, "HTTP"); // TODO test this
            path = string(path_ptr);
            break;
        }
    }

    // break down the path - get the last token after /,
    vector<string> pathTok = split(path);
    if (pathTok[pathTok.size() - 1] == "big_buck_bunny.f4m") {
        pathTok[pathTok.size() - 1] = "big_buck_bunny_nolist.f4m";
    }

    string ip;
    int port;

    // now actually establish all the needed connections
    if (args.dns) {
        // TODO: issue DNS query
        cout << "DNS Functionality incomplete" << endl;
        return;
    } else {
        ip = args.web_server_ip;
        port = 80; // Is this correct?
    }

    int server_sock = connect_to_server(ip, port);
    if (server_sock == -1)
        return;

    Bitrate_Tracker bt(args.alpha);

    pull_chunk_and_forward(client_fd, server_sock, pathTok, l, bt);

    // GET / HTTP/1.1
    // Host: localhost:8000
    // User-Agent: Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:59.0) Gecko/20100101 Firefox/59.0
    // Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8
    // Accept-Language: en-US,en;q=0.5
    // Accept-Encoding: gzip, deflate
    // Connection: keep-alive
    // Upgrade-Insecure-Requests: 1

    // TODO: parse request to get chunkname
    // TODO: get chunk from server
    // TODO: send chunk to client and get a clock time
    // TODO: log chunk info
    // TODO: check to see if I need to change bitrate

    // // setup socket to connect to servers
    // int server_sockfd = socket_init(stoi(args.web_server_ip));
    // // TODO: connect to server

    // fd_set current_sockets, ready_sockets;
    // FD_ZERO(current_sockets);
    // FD_SET(browser_sockfd, &current_sockets);

    // while(true) { // TODO: figure out when to exit loop
    //     ready_sockets = current_sockets;
    //     if(select(FD_SETSIZE, ready_sockets, NULL, NULL, NULL) < 0) {
    //         std::cout << "Select Error\n";
    //         exit();
    //     }

    //     for (int i = 0; i < FD_SETSIZE; ++i) {
    //         if(FD_ISSET(i, &ready_sockets)) {
    //             if(i == browser_sockfd) {
    //                 // new connection received
    //                 int connectionfd = socket_accept(browser_sockfd);
    //                 FD_SET(connectionfd, &current_sockets);
    //             }
    //             else {
    //                 // connection has data to read
    //                 // TODO: HANDLE CONNECTIONS HERE
    //             }
    //         }
    //     }
    // }
}

#endif