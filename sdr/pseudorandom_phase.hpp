#ifndef PSEUDORANDOM_PHASE_HPP
#define PSEUDORANDOM_PHASE_HPP

#include <random>

using namespace std;

// Random generator for phaase modulation
inline mt19937 random_generator(0);

float get_next_phase(); // Return a single float generated from random_generator
vector<float> get_next_n_phases(int n); // Return a vector of the next n phases from random_generator

#endif // PSEUDORANDOM_PHASE_HPP