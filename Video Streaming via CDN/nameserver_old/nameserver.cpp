
#include "DNSProtocol.h"
#include "Geographic.h"
#include "readFile.h"
#include "utils.h"
#include <arpa/inet.h>
#include <getopt.h>
#include <iostream>
#include <stdexcept>
#include <sys/select.h>

using std::cout;
using std::endl;
using std::string;

// TODO: look into the encoding/decoding functions
// TODO: Testing!

void help_string() {
    cout << "Usage: ./nameserver --geo <port> <serverfile> <log>" << endl;
    cout << "       ./nameserver --rr <port> <serverfile> <log>" << endl;
}

enum LBMode : bool { GEO, RR };

struct args_t {
    LBMode mode;
    bool rr;
    int port;
    string serverfile;
    string logfile;
};

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
    args.serverfile = server_file;

    string logFile = argv[optind + 2];
    args.logfile = logFile;
}

// Returns the server node closest to the source client
string geo_algorithm(string source_ip, Read &read) { // TEST TO MAKE SURE IT RETURNS CORRECT IP
    Graph G(read.num_nodes);
    G.buildEdges(read.links);
    cout << "Edges built" << endl;
    int source = -1;
    string closest_ip = "";

    vector<int> servers;

    for (size_t i = 0; i < read.geo_server.size(); ++i) {
        servers.push_back(read.geo_server[i].first);
        cout << servers[i] << endl;
    }

    for (size_t i = 0; i < read.geo_client.size(); ++i) {
        if (read.geo_client[i].second == source_ip)
            source = read.geo_client[i].first;
    }

    cout << "Finding closest ip server..." << source << endl;
    int closest_node = G.shortestPath(source, servers);

    for (size_t i = 0; i < read.geo_server.size(); ++i) {
        if (read.geo_server[i].first == closest_node)
            closest_ip = read.geo_server[i].second;
    }

    return closest_ip;
}

// Performs Geographic distance load balancing
void geoLB(response &resp, ofstream &logfile, string &client_ip, Read &read) { // Test this to see if it works
    cout << "Starting Geographic LB..." << endl;
    cout << "Client ip is: " << client_ip << endl;
    string closest_ip = geo_algorithm(client_ip, read);
    cout << "Closest ip is: " << closest_ip << endl;
    resp.ip = closest_ip;
    cout << "Response ip is: " << resp.ip << endl; // Should be the same as closest_ip
    cout << "Printing to Logfile: " << client_ip << " " << resp.hostname << " " << closest_ip << endl;
    logfile << client_ip << " " << resp.hostname << " " << closest_ip << endl;
}

void rrLB(response &resp, int &idx, string &client_ip, Read &read, ofstream &logfile) {
    cout << "Starting Round-Robin LB..." << endl;
    int num_rr_ip = read.rr_ip.size();
    resp.ip = read.rr_ip[idx % num_rr_ip];
    cout << "Printing to Logfile: " << client_ip << " " << resp.hostname << " " << resp.ip << endl;
    logfile << client_ip << " " << resp.hostname << " " << resp.ip << endl;
    idx++;
}

int main(int argc, char **argv) {
    args_t args;
    try {
        parse_opts(argc, argv, args);
    } catch (std::runtime_error &e) {
        std::cout << e.what() << std::endl;
        return 1;
    }
    ofstream logfile;

    // Open logfile for logging
    logfile.open(args.logfile);

    // RUN DNS
    Read read;
    if (args.mode == GEO)
        // Read from Geo File
        read.read_geo(args.serverfile);
    else
        // Read from RR file
        read.read_rr(args.serverfile);

    // initialize and Bind socket
    int master_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    struct sockaddr_in self;
    self.sin_family = AF_INET;
    self.sin_addr.s_addr = INADDR_ANY;
    self.sin_port = htons((u_short)args.port);
    int err = bind(master_socket, (struct sockaddr *)&self, sizeof(self));
    if (err == -1) {
        cout << "Error binding the socket to the port number\n";
        return 1;
    }

    err = listen(master_socket, 10);
    if (err == -1) {
        cout << "Error setting up listen queue\n";
        return 1;
    }

    fd_set readfds;
    vector<int> client_sockets;
    vector<string> client_ip;
    string hostname = "";
    ushort query_id = 0;
    int idx = 0;

    while (true) {
        FD_ZERO(&readfds);
        // Add socket to set
        FD_SET(master_socket, &readfds);

        for (size_t i = 0; i < client_sockets.size(); ++i) {
            FD_SET(client_sockets[i], &readfds);
        }

        cout << "Listening for activity..." << endl;
        int activity = select(FD_SETSIZE, &readfds, NULL, NULL, NULL); // check this later

        if (activity < 0) {
            cout << "Activity error";
            exit(1);
        }
        cout << "Activity detected" << endl;
        // Something happened on master socket
        if (FD_ISSET(master_socket, &readfds)) {
            struct sockaddr_in sock;
            __socklen_t addr_size = sizeof(sock);
            int new_socket = accept(master_socket, (struct sockaddr *)&sock, &addr_size);

            if (new_socket < 0)
                cout << "Error accepting" << endl;
            else { // recheck this later
                client_sockets.push_back(new_socket);
                char c[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, (struct sockaddr *)&sock.sin_addr.s_addr, c, sizeof c);
                client_ip.push_back(c);
            }
        }

        for (size_t i = 0; i < client_sockets.size(); ++i) {
            if (FD_ISSET(client_sockets[i], &readfds)) { // Start
                Query query;
                char buf[8 * 1024];
                memset(buf, 0, sizeof(buf));
                int header_size, question_size;
                int recvd = recv(client_sockets[i], &header_size, sizeof(header_size), 0);
                header_size = ntohl(header_size);
                if (recvd > 0)
                    cout << "Received header size from proxy: " << recvd << endl;
                else if (recvd == 0) {
                    client_sockets.erase(client_sockets.begin() + i);
                    client_ip.erase(client_ip.begin() + i);
                    cout << "Connection closed" << endl;
                } else {
                    cout << "Error while receiving request";
                    exit(1);
                }

                recvd = recv(client_sockets[i], buf, header_size, MSG_WAITALL);
                if (recvd > 0)
                    cout << "Received header from proxy: " << recvd << endl;
                else if (recvd == 0) {
                    client_sockets.erase(client_sockets.begin() + i);
                    client_ip.erase(client_ip.begin() + i);
                    cout << "Connection closed" << endl;
                } else {
                    cout << "Error while receiving request";
                    exit(1);
                }

                query.header = DNSHeader::decode(buf);

                recvd = recv(client_sockets[i], &question_size, sizeof(question_size), 0);
                question_size = ntohl(question_size);
                if (recvd > 0)
                    cout << "Received question size from proxy: " << recvd << endl;
                else if (recvd == 0) {
                    client_sockets.erase(client_sockets.begin() + i);
                    client_ip.erase(client_ip.begin() + i);
                    cout << "Connection closed" << endl;
                } else {
                    cout << "Error while receiving request";
                    exit(1);
                }

                recvd = recv(client_sockets[i], buf, question_size, 0);
                if (recvd > 0)
                    cout << "Received question from proxy: " << recvd << endl;
                else if (recvd == 0) {
                    client_sockets.erase(client_sockets.begin() + i);
                    client_ip.erase(client_ip.begin() + i);
                    cout << "Connection closed" << endl;
                } else {
                    cout << "Error while receiving request";
                    exit(1);
                }

                query.question = DNSQuestion::decode(buf);

                hostname = query.question.QNAME;

                cout << "Hostname is: " << hostname << endl;

                response resp(hostname, query_id);
                query_id++;
                if (query_id == 65535)
                    query_id = 0; // In case id gets too big

                if (args.mode == GEO)
                    geoLB(resp, logfile, client_ip[i], read);
                else
                    rrLB(resp, idx, client_ip[i], read, logfile);

                Response buffer = resp.get_buffer();

                string resp_header = DNSHeader::encode(buffer.header);
                string resp_record = DNSRecord::encode(buffer.record);

                header_size = sizeof(resp_header);
                header_size = htonl(header_size);
                int record_size = sizeof(resp_record);
                record_size = htonl(record_size);

                int resp_len = send(client_sockets[i], &header_size, sizeof(header_size), 0);

                resp_len = send(client_sockets[i], &resp_header, header_size, 0);

                resp_len = send(client_sockets[i], &record_size, sizeof(record_size), 0);

                resp_len = send(client_sockets[i], &resp_record, record_size, 0);

                if (resp_len < 0) {
                    cout << "Error";
                    exit(1);
                }

                /*// int resp_len = send(client_sockets[i], &buffer, sizeof(buffer), 0);
                int resp_len = send(client_sockets[i], ) if (resp_len <= 0) {
                    cout << "Error sending response to proxy ";
                    exit(1);
                }
                else cout << "Succcessfully sent response to proxy: " << resp_len << endl;*/
            }
        }
    }

    exit(0);
}