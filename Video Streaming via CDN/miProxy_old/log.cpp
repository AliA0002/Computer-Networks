#include "log.h"

using std::endl;
using std::ostream;

Log::Log(string filename) : log_file(filename) {}

void Log::log(const Log::entry &l) {
    log_file << l;
}

Log::~Log() {
    log_file.close();
}

ostream &operator<<(ostream &os, const Log::entry &l) {

    os << l.browser_ip << WHITESPACE << l.chunkname << WHITESPACE << l.server_ip << WHITESPACE << l.duration
       << WHITESPACE << l.tput << WHITESPACE << l.avg_tput << WHITESPACE << l.bitrate << endl;
    return os;
}