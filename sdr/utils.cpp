// Created 10/23/2021

#include <string>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include "utils.hpp"

using namespace std;

/** 
 * Change filename, e.g. from usrp_samples.dat to usrp_samples.00.dat,
 * but only if multiple names are to be generated.
 * 
 * Inputs: base_fn - base filename (e.g. usrp_samples.dat)
 *         n_names - number of filenames to be generated, corresponds to 
 *                   number of RX channels
 *         this_num - number corresponding to this filename/channel
 * Output: string holding the new filename (e.g. usrp_samples.00.dat) 
 */
std::string generate_out_filename(
        const std::string& base_fn, size_t n_names, size_t this_num)
{
    if (n_names == 1) {
        return base_fn;
    }

    boost::filesystem::path base_fn_fp(base_fn);
    base_fn_fp.replace_extension(boost::filesystem::path(
            str(boost::format("%02d%s") % this_num % base_fn_fp.extension().string())));
    return base_fn_fp.string();
}