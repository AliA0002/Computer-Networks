#ifndef _LOG_H_
#define _LOG_H_

#include "params.h"
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>

using std::ofstream;
using std::string;

class Log {
private:
  ofstream log;

public:
  Log(string filename);
  void write(string, string, string, double, double, double, int);
  void flush_log();
  ~Log();
};

#endif