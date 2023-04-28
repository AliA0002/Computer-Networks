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