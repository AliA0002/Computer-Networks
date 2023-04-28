
#include "DNSConnection.h"
#include "Log.h"
#include "Socket.h"
#include "params.h"
#include "utils.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cassert>
#include <chrono>
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
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using std::cout;
using std::deque;
using std::endl;
using std::istream;
using std::min;
using std::ofstream;
using std::pair;
using std::string;
using std::stringstream;
using std::thread;
using std::unordered_map;
using std::vector;
using std::chrono::duration_cast;
using std::chrono::nanoseconds;
using std::chrono::steady_clock;

void help_string() {
    cout << "Usage: ./miProxy --nodns <listen-port> <www-ip> <alpha> <log>" << endl;
    cout << "       ./miProxy --dns <listen-port> <dns-ip> <dns-port> "
            "<alpha> <log>"
         << endl;
}

struct args_t {
    uint16_t listen_port;

    DNSConnection *dns;

    float alpha;
    Log *log;
};

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

        int dns_port = atof(argv[optind + 2]);
        args.dns = new DNS(dns_ip, dns_port);

        float alpha = atof(argv[optind + 3]);
        args.alpha = alpha;

        string logFile = argv[optind + 4];
        args.log = new Log(logFile);
    } else {
        check_or_fail(argc - optind == 4, "Error: missing or extra arguments");
        int listen_port = atoi(argv[optind]);
        check_or_fail(listen_port > 0 && listen_port < 65536, "Error: Illegal Listen Port number");
        args.listen_port = listen_port;

        string www_ip = argv[optind + 1];
        check_or_fail(is_valid_ip(www_ip), "Error: Illegal Web Server IP address");
        args.dns = new NoDNS(www_ip);

        float alpha = atof(argv[optind + 2]);
        args.alpha = alpha;
        string logFile = argv[optind + 3];
        args.log = new Log(logFile);
    }
}

// thread
/*
make dns query
find manifest
while connection is open
    start sending bitrate
    move to VBR
*/

class BitrateTracker {
  public:
    BitrateTracker(double alpha, std::vector<int> avaliable_bitrates) {
        this->alpha = alpha;
        this->avaliable_bitrates = avaliable_bitrates;
        this->curTput = avaliable_bitrates[0];
    }
    void update(double tput) {
        this->curTput = alpha * tput + (1 - alpha) * this->curTput;
        cout << "@@@@@ Header:\n" << alpha << " " << tput << " " << curTput << " @@@@@";
    }
    double get_tput() { return this->curTput; }
    int get_bitrate() {
        int i = 0;
        while (i + 1 < avaliable_bitrates.size() && avaliable_bitrates[i + 1] * 1.5 <= this->curTput) {
            i++;
        }
        return avaliable_bitrates[i];
    }

  private:
    std::vector<int> avaliable_bitrates;
    double alpha;
    double curTput;
};

struct state_t {
    unordered_map<string, BitrateTracker> trackers;
    unordered_map<string, string> dns;
};

int headerEnd(char *buf, int len, int offset = 0) {
    if (offset < 3) {
        offset = 3;
    }
    for (int i = offset; i < len; i++)
        if (buf[i - 3] == '\r' && buf[i - 2] == '\n' && buf[i - 1] == '\r' && buf[i] == '\n')
            return i + 1;
    return -1;
}
pair<int, int> parseseg_frag(string s) {
    size_t spos = s.find("Seg") + 3;
    size_t fpos = s.find("Frag") + 4;
    if (spos == string::npos || fpos == string::npos) {
        return std::make_pair(0, 0);
    } else {
        size_t send = s.find('-', spos) - spos;
        size_t fend = s.find(' ', fpos) - fpos;
        int seg = atoi(s.substr(spos, send).c_str());
        int frag = atoi(s.substr(fpos, fend).c_str());
        return std::make_pair(seg, frag);
    }
}

string chunkname(pair<int, int> seg) {
    stringstream ss;
    ss << "Seg" << seg.first << "-Frag" << seg.second;
    return ss.str();
}

string switch_host(string header, string newHost) {
    size_t hoststart = header.find("Host: ") + 6;
    header.erase(hoststart, header.find("\r\n", hoststart) - hoststart); //  "Host: localhost\r\n"
    header.insert(hoststart, newHost);
    return header;
}

string switch_endpoint(string header, int bitrate, int seg, int frag) {
    size_t spos = header.find("Seg") + 3;
    size_t endpos = header.rfind("/", spos) + 1;
    size_t endend = header.find(" ", endpos);
    stringstream ss;
    ss << bitrate << "Seg" << seg << "-Frag" << frag;
    return header.erase(endpos, endend - endpos).insert(endpos, ss.str());
}
// TODO
/**
<?xml version="1.0" encoding="UTF-8"?>
<manifest xmlns="http://ns.adobe.com/f4m/1.0">
        <id>
                10
        </id>
        <streamType>
                recorded
        </streamType>
        <duration>
                596.50133333333338
        </duration>
        <bootstrapInfo
                 profile="named"
                 id="bootstrap5948"
        >
                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
        </bootstrapInfo>
        <media
                 streamId="10"
                 url="10"
                 bitrate="10"
                 bootstrapInfoId="bootstrap5948"
        >
                <metadata>
                        AgAKb25NZXRhRGF0YQgAAAAAAAhkdXJhdGlvbgBAgqQCuwz4fgAFd2lkdGgAQIqwAAAAAAAABmhlaWdodABAfgAAAAAAAAAMdmlkZW9jb2RlY2lkAgAEYXZjMQAMYXVkaW9jb2RlY2lkAgAEbXA0YQAKYXZjcHJvZmlsZQBAU0AAAAAAAAAIYXZjbGV2ZWwAQEQAAAAAAAAADnZpZGVvZnJhbWVyYXRlAEA+AAAAAAAAAA9hdWRpb3NhbXBsZXJhdGUAQOdwAAAAAAAADWF1ZGlvY2hhbm5lbHMAQAAAAAAAAAAACXRyYWNraW5mbwoAAAACAwAGbGVuZ3RoAEGJmJzAAAAAAAl0aW1lc2NhbGUAQPX5AAAAAAAACGxhbmd1YWdlAgADdW5kAAAJAwAGbGVuZ3RoAEF7TkAAAAAAAAl0aW1lc2NhbGUAQOdwAAAAAAAACGxhbmd1YWdlAgADZW5nAAAJAAAJ
                </metadata>
        </media>
        <media
                 streamId="100"
                 url="100"
                 bitrate="100"
                 bootstrapInfoId="bootstrap5948"
        >
                <metadata>
                        AgAKb25NZXRhRGF0YQgAAAAAAAhkdXJhdGlvbgBAgqQCuwz4fgAFd2lkdGgAQIqwAAAAAAAABmhlaWdodABAfgAAAAAAAAAMdmlkZW9jb2RlY2lkAgAEYXZjMQAMYXVkaW9jb2RlY2lkAgAEbXA0YQAKYXZjcHJvZmlsZQBAU0AAAAAAAAAIYXZjbGV2ZWwAQEQAAAAAAAAADnZpZGVvZnJhbWVyYXRlAEA+AAAAAAAAAA9hdWRpb3NhbXBsZXJhdGUAQOdwAAAAAAAADWF1ZGlvY2hhbm5lbHMAQAAAAAAAAAAACXRyYWNraW5mbwoAAAACAwAGbGVuZ3RoAEGJmJzAAAAAAAl0aW1lc2NhbGUAQPX5AAAAAAAACGxhbmd1YWdlAgADdW5kAAAJAwAGbGVuZ3RoAEF7TkAAAAAAAAl0aW1lc2NhbGUAQOdwAAAAAAAACGxhbmd1YWdlAgADZW5nAAAJAAAJ
                </metadata>
        </media>
        <media
                 streamId="500"
                 url="500"
                 bitrate="500"
                 bootstrapInfoId="bootstrap5948"
        >
                <metadata>
                        AgAKb25NZXRhRGF0YQgAAAAAAAhkdXJhdGlvbgBAgqQCuwz4fgAFd2lkdGgAQIqwAAAAAAAABmhlaWdodABAfgAAAAAAAAAMdmlkZW9jb2RlY2lkAgAEYXZjMQAMYXVkaW9jb2RlY2lkAgAEbXA0YQAKYXZjcHJvZmlsZQBAU0AAAAAAAAAIYXZjbGV2ZWwAQEQAAAAAAAAADnZpZGVvZnJhbWVyYXRlAEA+AAAAAAAAAA9hdWRpb3NhbXBsZXJhdGUAQOdwAAAAAAAADWF1ZGlvY2hhbm5lbHMAQAAAAAAAAAAACXRyYWNraW5mbwoAAAACAwAGbGVuZ3RoAEGJmJzAAAAAAAl0aW1lc2NhbGUAQPX5AAAAAAAACGxhbmd1YWdlAgADdW5kAAAJAwAGbGVuZ3RoAEF7TkAAAAAAAAl0aW1lc2NhbGUAQOdwAAAAAAAACGxhbmd1YWdlAgADZW5nAAAJAAAJ
                </metadata>
        </media>
        <media
                 streamId="1000"
                 url="1000"
                 bitrate="1000"
                 bootstrapInfoId="bootstrap5948"
        >
                <metadata>
                        AgAKb25NZXRhRGF0YQgAAAAAAAhkdXJhdGlvbgBAgqQCuwz4fgAFd2lkdGgAQIqwAAAAAAAABmhlaWdodABAfgAAAAAAAAAMdmlkZW9jb2RlY2lkAgAEYXZjMQAMYXVkaW9jb2RlY2lkAgAEbXA0YQAKYXZjcHJvZmlsZQBAU0AAAAAAAAAIYXZjbGV2ZWwAQEQAAAAAAAAADnZpZGVvZnJhbWVyYXRlAEA+AAAAAAAAAA9hdWRpb3NhbXBsZXJhdGUAQOdwAAAAAAAADWF1ZGlvY2hhbm5lbHMAQAAAAAAAAAAACXRyYWNraW5mbwoAAAACAwAGbGVuZ3RoAEGJmJzAAAAAAAl0aW1lc2NhbGUAQPX5AAAAAAAACGxhbmd1YWdlAgADdW5kAAAJAwAGbGVuZ3RoAEF7TkAAAAAAAAl0aW1lc2NhbGUAQOdwAAAAAAAACGxhbmd1YWdlAgADZW5nAAAJAAAJ
                </metadata>
        </media>
</manifest>

*/
vector<int> parse_manifest(string manifest) {
    size_t cur_loc = manifest.find("bitrate=") + 9;
    vector<int> brs;
    while (cur_loc != string::npos) {
        size_t endloc = manifest.find("\"", cur_loc);
        string br_s = manifest.substr(cur_loc, endloc - cur_loc);
        cur_loc = manifest.find("bitrate=", endloc) + 9;
        brs.push_back(stoi(br_s));
    }
    return brs;
}

size_t content_length(string header) {
    size_t contstart = header.find("Content-Length: ") + 16;
    string cont = header.substr(contstart, header.find("\r\n", contstart)); //  "Content-Length: 7015\r\n"
    return stoi(cont);
}

void handle_request(int fd, args_t *args, state_t *state) {
    socket_raii s(fd);

    string client_ip = get_ip_addr(fd);
    string server_ip;

    // first parse the request  - is this manifest or something else?
    char buf[BUF_SIZE];
    memset(buf, 0, BUF_SIZE);
    int offset = 0;
    int read_len = 0;
    while (headerEnd(buf, offset, offset - read_len) == -1) {
        read_len = socket_recv(fd, buf, BUF_SIZE - offset);
        offset += read_len;
    }

    // now i have the header...
    string header = string(buf, buf + offset);
    cout << "@@@@@ Header:\n" << header << "@@@@@";

    cout << "@@@@@ header: " << header << "\n@@@@@\n\n";

    // DNS request needed?
    string host = "video.cse.umich.edu"; //  "Host: localhost\r\n"
    {
        if (state->dns.find(client_ip) != state->dns.end()) { // I have it already
            server_ip = state->dns[client_ip];
        } else {
            server_ip = args->dns->resolve(host);
            state->dns[client_ip] = server_ip;
        }
    }

    header = switch_host(header, server_ip);

    size_t manPos = header.find(".f4m");

    pair<int, int> seg = parseseg_frag(header);
    if (manPos != string::npos) {
        {
            if (state->trackers.find(client_ip) != state->trackers.end()) {
                // Request 1 - build my tracker
                int full_manifest_fd = make_sock(server_ip.c_str(), 80);
                socket_raii fm(full_manifest_fd);
                socket_send_all(full_manifest_fd, header.c_str(), header.length());
                memset(buf, 0, BUF_SIZE);
                offset = 0;
                read_len = 0;
                while (headerEnd(buf, offset, offset - read_len) == -1) {
                    read_len = socket_recv(full_manifest_fd, buf, BUF_SIZE - offset);
                    offset += read_len;
                }
                char buf2[BUF_SIZE];
                int hend = headerEnd(buf, offset);
                memcpy(buf2, buf + hend, offset - hend);
                string full_man_resp_header = string(buf, buf + hend); // came back from server
                int cont_len = content_length(full_man_resp_header);
                socket_recv_all(full_manifest_fd, buf2 + (offset - hend), cont_len - (offset - hend));
                string manifest(buf, buf + cont_len);
                vector<int> man = parse_manifest(manifest);
                state->trackers.insert(make_pair(client_ip, BitrateTracker(args->alpha, man)));
            }
        }
        // So I have established a tracker here
        // No state for the below section
        {
            // Request 2
            header.insert(manPos, "_nolist");
            int nolist_manifest_fd = make_sock(server_ip.c_str(), 80);
            socket_raii nlm(nolist_manifest_fd);
            socket_send_all(nolist_manifest_fd, header.c_str(), header.length());
            memset(buf, 0, BUF_SIZE);
            offset = 0;
            read_len = 0;
            while (headerEnd(buf, offset, offset - read_len) == -1) {
                read_len = socket_recv(nolist_manifest_fd, buf, BUF_SIZE - offset);
                offset += read_len;
            }
            char buf2[BUF_SIZE];
            int hend = headerEnd(buf, offset);
            memcpy(buf2, buf + hend, offset - hend);
            string full_man_resp_header = string(buf, buf + hend); // came back from server
            // full_man_resp_header = switch_host(full_man_resp_header, host); // proxy host addr
            int cont_len = content_length(full_man_resp_header);
            // forward here - idc about contents
            socket_send_all(fd, full_man_resp_header.c_str(), full_man_resp_header.length());
            socket_send_all(fd, buf + hend, offset - hend);

            memset(buf2, 0, BUF_SIZE);
            offset = offset - hend;
            read_len = 0;
            while (offset < cont_len) {
                read_len = socket_recv(nolist_manifest_fd, buf2, min(cont_len - offset, BUF_SIZE));
                socket_send_all(fd, buf2, read_len);
                offset += read_len;
            }
        }
    } else if (seg.first != 0 && seg.second != 0) {
        int seg_fd = make_sock(server_ip.c_str(), 80);
        socket_raii seg_sr(seg_fd);
        {
            if (state->trackers.find(client_ip) == state->trackers.end()) {
                state->trackers.insert(make_pair(client_ip, BitrateTracker(args->alpha, {10, 100, 500, 1000})));
                cout << "@@@@@ Please don't run:"
                     << "@@@@@";
            }
            // assert(state->trackers.find(client_ip) != state->trackers.end());
            header = switch_endpoint(header, state->trackers.at(client_ip).get_bitrate(), seg.first, seg.second);

            socket_send_all(seg_fd, header.c_str(), header.length());
            cout << "@@@@@ Header:\n" << header << "@@@@@";
            memset(buf, 0, BUF_SIZE);
            offset = 0;
            read_len = 0;
            while (headerEnd(buf, offset, offset - read_len) == -1) {
                read_len = socket_recv(seg_fd, buf, BUF_SIZE - offset);
                offset += read_len;
            }
            char buf2[BUF_SIZE];
            int hend = headerEnd(buf, offset);
            memcpy(buf2, buf + hend, offset - hend);
            string resp_header = string(buf, buf + hend); // came back from server
            // resp_header = switch_host(resp_header, host); // proxy host addr
            int cont_len = content_length(resp_header);
            socket_send_all(fd, resp_header.c_str(), resp_header.length());
            socket_send_all(fd, buf + hend, offset - hend);
            memset(buf2, 0, BUF_SIZE);
            offset = offset - hend;
            read_len = 0;
            auto start = steady_clock::now();
            while (offset < cont_len) {
                read_len = socket_recv(seg_fd, buf2, min(cont_len - offset, BUF_SIZE));
                socket_send_all(fd, buf2, read_len);
                offset += read_len;
            }
            auto end = steady_clock::now();
            auto duration = duration_cast<nanoseconds>(end - start).count() / 1000000000.0;

            double tput = offset / 125. / duration;
            double brate = state->trackers.at(client_ip).get_bitrate();

            state->trackers.at(client_ip).update(tput);

            args->log->write(client_ip, chunkname(seg), server_ip, duration, tput,
                             state->trackers.at(client_ip).get_tput(), brate);
            args->log->flush_log();
        }
        // so the segment has been forwarded?
    } else { // index or others...
        int other_fd = make_sock(server_ip.c_str(), 80);
        socket_raii sr(other_fd);
        socket_send_all(other_fd, header.c_str(), header.length());
        memset(buf, 0, BUF_SIZE);
        offset = 0;
        read_len = 0;
        while (headerEnd(buf, offset, offset - read_len) == -1) {
            read_len = socket_recv(other_fd, buf, BUF_SIZE - offset);
            offset += read_len;
        }
        char buf2[BUF_SIZE];
        int hend = headerEnd(buf, offset);
        memcpy(buf2, buf + hend, offset - hend);
        string resp_header = string(buf, buf + hend); // came back from server

        // resp_header = switch_host(resp_header, host); // proxy host addr
        int cont_len = content_length(resp_header);
        cout << "@@@@@ Response Len: " << hend << "\n" << resp_header << "\n@@@@@\n\n";
        // forward here - idc about contents
        socket_send_all(fd, resp_header.c_str(), resp_header.length());
        socket_send_all(fd, buf + hend, offset - hend);
        // cout << "@@@@@ Piece Len: " << offset - hend << "\n" << string(buf + hend, buf + offset) << "\n@@@@@\n\n";
        memset(buf2, 0, BUF_SIZE);
        offset = offset - hend;
        read_len = 0;
        while (offset < cont_len) {
            read_len = socket_recv(other_fd, buf2, min(cont_len - offset, BUF_SIZE));
            socket_send_all(fd, buf2, read_len);
            // cout << "@@@@@ Piece Len: " << read_len << "\n" << string(buf2, buf2 + read_len) << "\n@@@@@\n\n";
            offset += read_len;
        }
    }
}

int main(int argc, char **argv) {
    args_t args;
    parse_opts(argc, argv, args);

    int sockfd = socket_init(args.listen_port);
    if (sockfd == -1) {
        return -1;
    }

    args.listen_port = socket_getPort(sockfd);
    // (4) Begin listening for incoming connections.
    listen(sockfd, 10);

    state_t state;

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
                handle_request(fds[i], &args, &state);
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
