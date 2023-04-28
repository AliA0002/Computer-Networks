#include "Log.h"

using std::endl;
using std::string;

Log::Log(string filename) : log(filename) {}
void Log::write(string browser_ip, string chunkname, string server_ip, double duration, double tput, double avg_tput,
                int bitrate) {
  log << browser_ip << " " << chunkname << " " << server_ip << " " << duration << " " << tput << " " << avg_tput << " "
      << bitrate << endl;
}
void Log::flush_log () {
    log.flush();
}
Log::~Log() { log.close(); }