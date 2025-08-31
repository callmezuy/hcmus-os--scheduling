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
  int total_cpu;       // total CPU burst time
  int total_resource;  // total resource burst time
  int resource_start;  // time when process is allowed to begin its resource
                       // burst
  bool hasRun;         // false if process hasn't run yet
  int total_waiting_in_ready =
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
                vector<Process> &processes) {
  ifstream fin(filename);
  if (!fin) {
    cerr << "Error opening input file.\n";
    exit(1);
  }
  vector<string> lines;
  string line;
  while (getline(fin, line)) {
    if (!line.empty()) lines.push_back(line);
  }
  fin.close();

  // First line: scheduling algorithm.
  algorithm = stoi(lines[0]);
  int index = 1;
  if (algorithm == 2) {  // For Round Robin, next line is time quantum.
    timeQuantum = stoi(lines[index]);
    index++;
  } else {
    timeQuantum = 0;
  }
  int numProcesses = stoi(lines[index]);
  index++;
  processes.clear();
  for (int i = 0; i < numProcesses; i++) {
    Process proc;
    proc.id = i + 1;
    proc.current_op = 0;
    proc.finish = 0;
    proc.resource_start = 0;
    proc.hasRun = false;
    proc.total_waiting_in_ready = 0;
    istringstream iss(lines[index]);
    index++;
    iss >> proc.arrival;
    proc.orig_arrival = proc.arrival;
    proc.total_cpu = 0;
    proc.total_resource = 0;
    string token;
    while (iss >> token) {
      Operation op;
      size_t pos = token.find('(');
      if (pos != string::npos) {
        op.duration = stoi(token.substr(0, pos));
        op.resource = token.substr(
            pos + 1, token.size() - pos - 2);  // remove parentheses
        op.type = RESOURCE;
        proc.total_resource += op.duration;
      } else {
        op.duration = stoi(token);
        op.type = CPU;
        proc.total_cpu += op.duration;
      }
      proc.ops.push_back(op);
    }
    if (!proc.ops.empty())
      proc.remaining = proc.ops[0].duration;
    else
      proc.remaining = 0;
    proc.last_cpu = proc.arrival;
    processes.push_back(proc);
  }
}

SimulationResult simulate(int algorithm, int timeQuantum,
                          vector<Process> &processes) {
  SimulationResult result;
  int time = 0;
  vector<string> cpuGantt;

  // Determine all resource names used.
  map<string, bool> resNames;
  for (auto &p : processes) {
    for (auto &op : p.ops) {
      if (op.type == RESOURCE) resNames[op.resource] = true;
    }
  }
  // Initialize resource timelines and queues.
  map<string, vector<string>> resourceGantt;
  map<string, Process *> resourceCurrent;
  map<string, vector<Process *>> resourceQueue;
  for (auto &entry : resNames) {
    resourceGantt[entry.first] = vector<string>();
    resourceCurrent[entry.first] = nullptr;
    resourceQueue[entry.first] = vector<Process *>();
  }

  Process *cpuCurrent = nullptr;
  int rrCounter = 0;  // remaining quantum for RR
  vector<Process *> cpuQueue;

  // List of processes not yet arrived.
  vector<Process *> notArrived;
  for (auto &p : processes) notArrived.push_back(&p);

  int finishedCount = 0;
  int totalProcesses = processes.size();

  // Simulation loop.
  while (finishedCount < totalProcesses || cpuCurrent != nullptr ||
         !cpuQueue.empty() || ([&]() {
           for (auto &entry : resourceCurrent)
             if (entry.second != nullptr) return false;
           for (auto &entry : resourceQueue)
             if (!entry.second.empty()) return false;
           return true;
         }() == false) ||
         !notArrived.empty()) {
    // Increment waiting time only for processes in the CPU queue that were
    // enqueued in a previous tick.
    for (auto p : cpuQueue) {
      if (p->arrival < time) {
        p->total_waiting_in_ready++;
      }
    }

    // Enqueue newly arrived processes.
    for (auto it = notArrived.begin(); it != notArrived.end();) {
      if ((*it)->arrival <= time) {
        cpuQueue.push_back(*it);
        it = notArrived.erase(it);
      } else {
        ++it;
      }
    }

    // Sort CPU ready queue.
    if (algorithm == 1 || algorithm == 2) {
      // For FCFS or RR: sort by arrival, then new processes get priority.
      stable_sort(cpuQueue.begin(), cpuQueue.end(), [](Process *a, Process *b) {
        if (a->arrival != b->arrival) return a->arrival < b->arrival;
        if (a->hasRun != b->hasRun)
          return !a->hasRun;  // new process (hasRun==false) gets priority.
        return a->last_cpu < b->last_cpu;
      });
    } else if (algorithm == 3 || algorithm == 4) {
      // For SJF/SRTN: sort primarily by remaining time.
      stable_sort(cpuQueue.begin(), cpuQueue.end(), [](Process *a, Process *b) {
        if (a->remaining != b->remaining) return a->remaining < b->remaining;
        if (a->hasRun != b->hasRun) return !a->hasRun;
        if (a->arrival != b->arrival) return a->arrival < b->arrival;
        return a->last_cpu < b->last_cpu;
      });
    }

    // For SRTN: preempt if a process in queue has a smaller remaining time,
    // or if remaining times are equal and the ready process is new.
    if (algorithm == 4 && cpuCurrent != nullptr) {
      if (!cpuQueue.empty() &&
          (cpuQueue[0]->remaining < cpuCurrent->remaining ||
           (cpuQueue[0]->remaining == cpuCurrent->remaining &&
            (!cpuQueue[0]->hasRun && cpuCurrent->hasRun)))) {
        cpuQueue.push_back(cpuCurrent);
        cpuCurrent = nullptr;
      }
    }

    // If CPU is idle, pick next process.
    if (cpuCurrent == nullptr && !cpuQueue.empty()) {
      cpuCurrent = cpuQueue.front();
      cpuQueue.erase(cpuQueue.begin());
      // When scheduling on the CPU, mark as having run.
      if (!cpuCurrent->hasRun) cpuCurrent->hasRun = true;
      if (cpuCurrent->current_op < (int)cpuCurrent->ops.size()) {
        Operation &op = cpuCurrent->ops[cpuCurrent->current_op];
        if (op.type == CPU && cpuCurrent->remaining <= 0)
          cpuCurrent->remaining = op.duration;
        if (algorithm == 2) rrCounter = timeQuantum;
      }
    }

    // CPU execution.
    if (cpuCurrent) {
      cpuGantt.push_back(to_string(cpuCurrent->id));
      cpuCurrent->remaining--;
      if (algorithm == 2) rrCounter--;
      if (cpuCurrent->remaining == 0) {
        cpuCurrent->last_cpu = time + 1;
        cpuCurrent->current_op++;
        if (cpuCurrent->current_op < (int)cpuCurrent->ops.size()) {
          Operation &nextOp = cpuCurrent->ops[cpuCurrent->current_op];
          if (nextOp.type == RESOURCE) {
            cpuCurrent->resource_start =
                time + 1;  // Resource burst begins no earlier than next tick.
            resourceQueue[nextOp.resource].push_back(cpuCurrent);
            cpuCurrent->remaining = nextOp.duration;
          } else if (nextOp.type == CPU) {
            cpuCurrent->remaining = nextOp.duration;
            cpuQueue.push_back(cpuCurrent);
          }
        } else {
          cpuCurrent->finish = time + 1;
          finishedCount++;
        }
        cpuCurrent = nullptr;
      } else {
        if (algorithm == 2 && rrCounter == 0) {
          cpuCurrent->arrival = time + 1;
          cpuQueue.push_back(cpuCurrent);
          cpuCurrent = nullptr;
        }
      }
    } else {
      cpuGantt.push_back("_");
    }

    // Resource execution.
    for (auto &entry : resourceGantt) {
      string resName = entry.first;
      // If resource is idle and there's a waiting process, assign it only if
      // its resource_start <= time.
      if (resourceCurrent[resName] == nullptr &&
          !resourceQueue[resName].empty()) {
        if (resourceQueue[resName].front()->resource_start <= time) {
          Process *proc = resourceQueue[resName].front();
          resourceQueue[resName].erase(resourceQueue[resName].begin());
          resourceCurrent[resName] = proc;
        }
      }
      if (resourceCurrent[resName]) {
        resourceGantt[resName].push_back(
            to_string(resourceCurrent[resName]->id));
        resourceCurrent[resName]->remaining--;
        if (resourceCurrent[resName]->remaining == 0) {
          resourceCurrent[resName]->last_cpu = time + 1;
          resourceCurrent[resName]->current_op++;
          if (resourceCurrent[resName]->current_op <
              (int)resourceCurrent[resName]->ops.size()) {
            Operation &nextOp = resourceCurrent[resName]
                                    ->ops[resourceCurrent[resName]->current_op];
            if (nextOp.type == CPU) {
              resourceCurrent[resName]->remaining = nextOp.duration;
              resourceCurrent[resName]->arrival = time + 1;
              cpuQueue.push_back(resourceCurrent[resName]);
            } else if (nextOp.type == RESOURCE) {
              resourceQueue[nextOp.resource].push_back(
                  resourceCurrent[resName]);
              resourceCurrent[resName]->remaining = nextOp.duration;
            }
          } else {
            resourceCurrent[resName]->finish = time + 1;
            finishedCount++;
          }
          resourceCurrent[resName] = nullptr;
        }
      } else {
        resourceGantt[resName].push_back("_");
      }
    }

    time++;
  }

  // Compute turnaround and waiting times.
  vector<int> turnaround(processes.size(), 0);
  vector<int> waiting(processes.size(), 0);
  for (auto &p : processes) {
    int t = p.finish - p.orig_arrival;
    turnaround[p.id - 1] = t;
    waiting[p.id - 1] = p.total_waiting_in_ready;
  }

  result.cpuGantt = cpuGantt;
  result.resourceGantt = resourceGantt;
  result.turnaround = turnaround;
  result.waiting = waiting;
  return result;
}

void writeOutput(const string &filename, SimulationResult &res,
                 int numProcesses) {
  ofstream fout(filename);
  if (!fout) {
    cerr << "Error opening output file.\n";
    exit(1);
  }
  // Write CPU Gantt chart.
  for (size_t i = 0; i < res.cpuGantt.size(); i++) {
    fout << res.cpuGantt[i] << (i < res.cpuGantt.size() - 1 ? " " : "");
  }
  fout << "\n";
  // Write resource timelines (only those with non-idle entries).
  for (auto &entry : res.resourceGantt) {
    bool used = false;
    for (auto &s : entry.second)
      if (s != "_") {
        used = true;
        break;
      }
    if (used) {
      for (size_t i = 0; i < entry.second.size(); i++) {
        fout << entry.second[i] << (i < entry.second.size() - 1 ? " " : "");
      }
      fout << "\n";
    }
  }
  // Write turnaround times.
  for (int i = 0; i < numProcesses; i++) {
    fout << res.turnaround[i] << (i < numProcesses - 1 ? " " : "");
  }
  fout << "\n";
  // Write waiting times.
  for (int i = 0; i < numProcesses; i++) {
    fout << res.waiting[i] << (i < numProcesses - 1 ? " " : "");
  }
  fout << "\n";
  fout.close();
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    cout << "Usage: MSSV1_MSSV2.exe <INPUT_FILE> <OUTPUT_FILE>\n";
    return 1;
  }
  string inputFile = argv[1];
  string outputFile = argv[2];

  int algorithm, timeQuantum;
  vector<Process> processes;
  parseInput(inputFile, algorithm, timeQuantum, processes);

  SimulationResult simResult = simulate(algorithm, timeQuantum, processes);

  writeOutput(outputFile, simResult, processes.size());

  return 0;
}
