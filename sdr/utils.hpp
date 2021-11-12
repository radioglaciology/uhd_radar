// Created 10/23/2021

#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>

 // Change filename, e.g. from usrp_samples.dat to usrp_samples.00.dat,
 // if multiple filenames should be generated
std::string generate_out_filename(
        const std::string& base_fn, size_t n_names, size_t this_name);

#endif //UTILS_HPP
