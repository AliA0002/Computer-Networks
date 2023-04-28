#include "utils.h"
#include <fstream>

using std::cout;
using std::endl;

void check_or_fail(bool condition, string msg) {
    if (!condition) {
        cout << msg << endl;
        exit(1);
    }
}

bool is_valid_ip(string ip) { return true; }

ifstream open_file(string path) { return ifstream(); }