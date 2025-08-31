#pragma once

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

enum BurstType { CPU, RESOURCE };

struct Operation {
  BurstType type;
  int duration;
  string resource;  // only used if type == RESOURCE
};

struct Process {
  int id;
  int arrival;       // current arrival time (may be updated when requeued)
  int orig_arrival;  // original arrival (for turnaround calculation)
  vector<Operation> ops;
  int current_op;      // index of current operation
  int remaining;       // remaining time in current operation
  int last_cpu;        // used for tie-breaking
  int finish;          // finish time
  int resource_start;  // time when process is allowed to begin its resource
                       // burst
  bool hasRun;         // false if process hasn't run yet
  int total_waiting =
      0;  // accumulated waiting ticks in CPU ready queue
};

struct SimulationResult {
  vector<string>
      cpuGantt;  // CPU timeline (each tick: process id or "_" for idle)
  map<string, vector<string>>
      resourceGantt;       // resource timelines keyed by resource name
  vector<int> turnaround;  // turnaround time for each process (index = id - 1)
  vector<int> waiting;     // waiting time for each process (index = id - 1)
};

void parseInput(const string &filename, int &algorithm, int &timeQuantum,
                vector<Process> &processes);
SimulationResult simulate(int algorithm, int timeQuantum,
                          vector<Process> &processes);
void writeOutput(const string &filename, SimulationResult &res,
                 int numProcesses);
