#include <fstream>
#include <iostream>
#include <string>

using std::ifstream;
using std::string;
void check_or_fail(bool, string);

bool is_valid_ip(string ip);

ifstream open_file(string path);