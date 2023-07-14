import shutil
import glob
import argparse
import re
import os

if __name__ == "__main__":
    # Accept one command line argument, a string called prefix
    parser = argparse.ArgumentParser()
    parser.add_argument("prefix", 
            help='Prefix of files to be merged, such as "data/20230713"')
    parser.add_argument("--output", required=False,
                        help='Output file name. If left blank, will be prefix + "_rx_samps.bin"')
    args = parser.parse_args()

    path = args.prefix + '_p*_rx_samps.bin'
    files = glob.glob(path, recursive=False)
    file_ordering = {}
    for filename in files:
        filename_search = re.search('\d{8}_\d{6}_p(\d+)_rx_samps.bin', filename)
        if filename_search:
            file_ordering[int(filename_search.group(1))] = filename

    file_idxs = list(file_ordering.keys())
    file_idxs.sort()

    print("Found files:")
    for idx in file_idxs:
        print(f"[{idx}] {file_ordering[idx]}")

    # Sanity check file list to see if we're missing anything, have duplicates, or have no files
    if len(set(file_idxs)) != len(file_idxs):
        print("Error: duplicate file indices found.")
        exit(1)
    
    if len(file_idxs) == 0:
        print("Error: no files found.")
        exit(1)

    if len(file_idxs) != file_idxs[-1] + 1:
        print("Error: missing file indices.")
        exit(1)

    # Generate output filename
    if args.output is None:
        args.output = args.prefix + '_rx_samps.bin'
    
    # Check if it already exists
    if os.path.exists(args.output):
        print(f"Error: output file {args.output} already exists.")
        exit(1)
    
    print(f"\nEverything looks OK. Merging files to {args.output}...")
    with open(args.output, 'wb') as outfile:
        for idx in file_idxs:
            print(f"Copying {file_ordering[idx]}...")
            shutil.copyfileobj(open(file_ordering[idx], 'rb'), outfile)

    print(f"Done. Merged data written to {args.output}")