#include "func.cpp"
#include "header.h"

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
