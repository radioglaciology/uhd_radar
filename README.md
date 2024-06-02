# Open Radar Code Architecture (ORCA)

This repository contains a unified set of code for operating coherent, chirped radar systems USRP-based software-defined radios. ORCA was developed by Stanford Radio Glaciology to increase the accesibility of radar sounder instruments in the glaciological community. Most of Stanford Radio Glaciology's USRP-based radar instruments, including MAPPERR and PEREGRINE, run on this code.

## Repository Organization

* `config/` contains YAML files encapsulating all of the settings needed to run various experiments on different hardware setups. Ideally, the same code should be able to be run for every SDR and every type of measurement with a different YAML file from this folder determining all of the necessary settings and parameters.
* `data/` is where you can locally store whatever results/outputs your experiment creates. Everything (except the readme) in this folder will be ignored by git. Please don't check your specific results into version control (but do back them up somewhere).
* `preprocessing/` contains any scripts that need to run BEFORE the SDR code but that don't directly interface with the SDR. (For example: generation of a chirp waveform.)
* `sdr/` contains any code that directly controls the SDR
* `postprocessing/` contains any code that processing or plots data recorded by the SDR without directly interfacing with the SDR.
* `run.py` is a utility that manages the whole process of generating your output chirp, compiling the C++ code, running the radar, and collecting your results. This is the recommended way to run the radar system.

## Configuring your environment

The easiest way to make sure you have the right dependencies to run everything here is to use conda. If you're new to conda, there are some notes on setting things up [here](conda.md).

Dependencies for this project are managed through the `environment.yaml` file. You can create a conda environment with everything you need to run this code like this:

`conda env create -n myenvironmentname -f environment.yaml`

(Specifying `-n myenvironmentname` is optional. The default, as specified in `environment.yaml` is `uhd`.)

If you are setting an environment up on a Raspberry Pi, we recommend using `environment-rpi.yaml` instead. This version includes additional dependencies used by `manager/uav_payload_manager.py`, a helper script designed to run only on Raspberry Pi-based radar instruments.

Then activate it like this:

`conda activate myenvironmentname`

And you're good to go. This installs UHD and all the other necessary dependencies.

For directly interacting with the SDRs, you will need to download the FPGA images. After activating the environment, you can do this by running: `uhd_images_downloader`

### Running the code

Everything about the experiment you want to run is defined by a configuration YAML file. You can take a look at the examples in the `config/` directory,
but you'll likely need to create your own file for whatever you want to do specifically. The `config/default.yaml` file contains comments explaining roughly what each parameter does.

The recommended way of running everything is by using the `run.py` utility. In general, you run it like this:

`python run.py config/your_config_file.yaml`

This utility handles creating the chirp that will be transmitted, compiling and running the C++ code that interacts with the SDR, and collecting the output data.
All of the configuration for this is contained within your configuration YAML file.

## Information for developers and troubleshooting tips

### Adding a dependency

If you need to add a new package, you can update `environment.yaml`. It's probably easiest to just do this manually, however, if you really want, you can also update it by exporting your existing environment:

`conda env export --from-history > environment.yaml`

Please check that the changes you made are what you expected before committing them. Also, please do not do this without the `--from-history` command. The full environment export is a nightmare to debug across multiple platforms. When in doubt, manually update it. And always test it out by using the new `environment.yaml` to create a new environment.

### Using Visual Studio Code

It takes a few extra steps to tell Visual Studio Code that you're using the conda environment. For setup instructions, [see here](vscode.md).

## Adding features, git conventions

The basic workflow for adding features in should be something like this:

1. Create your own branch of this repository: `git checkout -b thomas/cool-new-feature`
   
   The `name/` thing is just a convention, but it's a nice way of who is working on what. Also, many git GUIs (such as Sublime Merge) will sort different branches into folders so you can easily see all of your (or someone else's) branches.

2. Prototype whatever changes you want to make. Do whatever it takes to make it work.

3. (Optional) You might want to commit these changes (`git add <changed files>`, `git commit -m "what I did"`) and then push the changes to GitHub (`git push --set-upstream origin thomas/cool-new-feature`). This will push your changes to GitHub, still in a separate branch, allowing others to see your branch along with the `main` branch and all the others.

3. Figure out how to cleanly integrate whatever your doing with the rest of the code. This will probably mean making it possible to configure your feature or disable it completley with only changes to a YAML config file. Then make sure that the defaults are to not use it (if the feature won't be needed by others), so your changes don't break someone else's instrument.

4. Commit again. Push your changes. (See #3.)

5. Go to GitHub and you'll be prompted to create a [pull request](https://docs.github.com/en/github/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/about-pull-requests). Pull requests are a GitHub feature that lets you and others preview what happens if you merge your branch into the `main` branch. You can send others a link so we can all review the changes you're propsoing, including making comments on them and discussing if there are any issues.


## Miscellaneous Notes
### Symbol Errors when Compiling 
If you get errors like this: 
```
dyld: lazy symbol binding failed: Symbol not found: __ZN5boost24scoped_static_mutex_lockC1ERNS_12static_mutexEb
  Referenced from: /opt/local/lib/libuhd.3.15.0.dylib
  Expected in: /opt/local/lib/libboost_regex-mt.dylib

dyld: Symbol not found: __ZN5boost24scoped_static_mutex_lockC1ERNS_12static_mutexEb
  Referenced from: /opt/local/lib/libuhd.3.15.0.dylib
  Expected in: /opt/local/lib/libboost_regex-mt.dylib

./run_default.sh: line 8: 60892 Abort trap: 6
```
you should invalidate (clear/clean/delete) your `CMakeCache.txt` file (located in your `build/` directory) and double check that in `CMakeLists.txt` you have the include and library paths set to your conda installation:

```
set(CMAKE_LIBRARY_PATH "/Users/abroome/opt/miniconda3/envs/srg_uhd_radar/lib")
set(CMAKE_INCLUDE_PATH "/Users/abroome/opt/miniconda3/envs/srg_uhd_radar/include")
```

### Right Shift Operator Warnings
If you get warnings like this:
```
space required between adjacent '>' delimeters of nested template argument lists ('>>' is the right shift operator)
```
check your `.vscode/c_cpp_properties.json` file and make sure that the C++ standard is set to at least 11: `"cppStandard": "c++11"`. 

### Running on X310
The `x310_startup.sh` script should be run once when first connecting to the X310. 


## Potentially Helpful Ettus/NI Application Notes
* [Timed commands](https://kb.ettus.com/Synchronizing_USRP_Events_Using_Timed_Commands_in_UHD)
