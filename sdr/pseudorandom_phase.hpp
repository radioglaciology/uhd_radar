#ifndef PSEUDORANDOM_PHASE_HPP
#define PSEUDORANDOM_PHASE_HPP

#include <random>

using namespace std;

// Random generators for phaase modulation
// Seed is identical (and hard-coded) so they will each produce the same sequence
inline mt19937 random_generator_tx(0); // Used on transmit for phase modulation
inline mt19937 random_generator_rx(0); // Used on receive for inverting phase modulation

float get_next_phase(bool transmit); // Return a single float generated from random_generator
vector<float> get_next_n_phases(int n, bool transmit); // Return a vector of the next n phases from random_generator

#endif // PSEUDORANDOM_PHASE_HPP