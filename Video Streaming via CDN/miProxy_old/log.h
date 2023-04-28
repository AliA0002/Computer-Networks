#ifndef _LOG_H_
#define _LOG_H_

#include <fstream>
#include <params.h>
#include <sstream>
#include <string>

using std::ostream;
using std::string;
class Log {
  private:
    std::ofstream log_file;

  public:
    Log(string filename);
    ~Log();
    struct entry {
        string browser_ip;
        string chunkname;
        string server_ip;
        long duration;
        double tput;
        double avg_tput;
        int bitrate;
        friend ostream &operator<<(ostream &os, const entry &l);
    };
    void log(const entry &);
};
ostream &operator<<(ostream &os, const Log::entry &l);

#endif