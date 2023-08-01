#include <random>
#include "pseudorandom_phase.hpp"

using namespace std;


// Return a single float generated from random_generator
float get_next_phase(){
    return (float) random_generator();
}

// Return a vector of the next n phases
vector<float> get_next_n_phases(int n){
    vector<float> ph(n);
    for(int i=0;i<n;i++) {
        ph[i] = get_next_phase();
    }
    return ph;
}