#include "readFile.h"

using namespace std;

// Read in geo file
void Read::read_geo(string fileName) {
    string line, type, ip;
    int num_links, num;
    int origin_id, des_id, cost;
    vector<int> link;
    ifstream file(fileName);

    if (file.is_open()) {
        file >> line >> num_nodes;
        for (int i = 0; i < num_nodes; ++i) {
            file >> num >> type >> ip;

            if (type == "CLIENT") {
                geo_client.push_back(make_pair(num, ip));

            } else if (type == "SWITCH") {
                geo_switch.push_back(make_pair(num, ip));

            } else if (type == "SERVER") {
                geo_server.push_back(make_pair(num, ip));

            } else {
                cerr << "Incorrect type";
                exit(0);
            }
        }
        file >> line >> num_links;

        for (int i = 0; i < num_links; ++i) {
            file >> origin_id >> des_id >> cost;
            link.push_back(origin_id);
            link.push_back(des_id);
            link.push_back(cost);
            links.push_back(link);
        }
    }
    file.close();
}

// Read in round-robin file
void Read::read_rr(string fileName) {
    string line;
    ifstream file(fileName);
    if (file.is_open()) {
        while (file >> line) {
            rr_ip.push_back(line);
        }
    }
    file.close();
}