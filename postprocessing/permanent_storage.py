import os
import shutil
import glob
import argparse


# Function to move timestamped files from source directory to destination directory
# Usage: To transfer to external storage, group specific timestamps together
# Dont know if binary or processed yet
## Example: python move_timestamped_files.py 20231001_120000 -- source_dir data --dest_dir permanent_storage
# Arg1: timestamp in format YYYYMMDD_HHMMSS
# Arg2: source directory containing files to move (default: data)
# Arg3: destination directory for permanent storage

def move_timestamped_files(timestamp, source_dir, dest_dir):
   os.makedirs(dest_dir, exist_ok=True)
   pattern = os.path.join(source_dir, f"{timestamp}_*.bin")
   files = glob.glob(pattern)
   if not files:
      print(f"No files found for timestamp {timestamp} in {source_dir}")
      return
   for file in files: 
      dest_path = os.path.join(dest_dir, os.path.basename(file))
      shutil.move(file, dest_path)
      print(f"Moved {file} -> {dest_path}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description= "Move timestamped files to permanent storage")
    parser.add_argument("timestamp", help="Timestamp to match files (format: YYYYMMDD_HHMMSS)")
    parser.add_argument("--source_dir", default="data", help="Source directory (default: data)")
    parser.add_argument("--dest_dir", required=True, help="Destination directory for permanent storage")# maybe add default later
    args = parser.parse_args()
    move_timestamped_files(args.timestamp, args.source_dir, dest_dir= args.dest_dir)
