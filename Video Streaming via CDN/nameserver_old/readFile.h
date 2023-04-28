// Reads in File
#ifndef _READFILE_H_
#define _READFILE_H_

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

class Read {

  public:
    vector<string> rr_ip;
    vector<pair<int, string>> geo_client;
    vector<pair<int, string>> geo_server;
    vector<pair<int, string>> geo_switch;
    vector<vector<int>> links;
    int num_nodes;
    void read_geo(string fileName);
    void read_rr(string fileName);
};

#endif