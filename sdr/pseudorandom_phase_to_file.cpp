#include <iostream>
#include <fstream>
#include <vector>
#include "pseudorandom_phase.hpp"

using namespace std;

int main(int argc, char *argv[]) {
    if (argc != 3) {
        cout << "Usage: " << argv[0] << " <n> <filename>" << endl;
        cout << "n is the number of random phases to produce" << endl;
        cout << "filename is a path to write the phases to. Each phase is a floating point value. The file is binary file with each float." << endl;
        return 1;
    }

    int n = atoi(argv[1]);
    if (n <= 0) {
        cout << "Invalid value for n. Please provide a positive integer." << endl;
        return 1;
    }

    vector<float> phases = get_next_n_phases(n, true);
    string filename = argv[2];

    ofstream outputFile(filename, ios::binary | ios::out);
    if (!outputFile.is_open()) {
        cout << "Error opening the file " << filename << endl;
        return 1;
    }

    // Writing the phases as binary data
    outputFile.write(reinterpret_cast<const char*>(phases.data()), sizeof(float) * phases.size());

    outputFile.close();
    cout << "Phases written to " << filename << " successfully." << endl;

    return 0;
}
