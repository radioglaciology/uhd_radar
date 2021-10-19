# Using VSCode as your editor

If you want to use Visual Studio Code as your IDE, it'll make your life easier if you tell it about your conda environment.

Install the `Python`, `C/C++`, and `CMake Tools` extensions.

## Python setup

Open the command palette (Ctrl-Shift-P or F1) and select `Python: Select Interpreter`. Hopefully, you'll see a path corresponding to a python binary in your conda environment. Select that one.

## C++ setup

You'll need to create a script somewhere that activates your choosen conda environment. You can put it anywhere you like. It should be something like this:

```bash
#!/bin/bash
source <path to your conda/miniconda installation>/etc/profile.d/conda.sh
conda activate <name of your conda environment>
```

Open the command palette again and select `CMake: Edit User-Local CMake Kits`

Copy the existing entry and create a new entry after it. Change the name to something you can recognize. Add a new paramter `environmentSetupScript` with a value of the full path to the activation script you just created.

```json
{
    "name": "UHD Environment GCC 10.3.0 x86_64-linux-gnu",
    "compilers": {
      "C": "/bin/x86_64-linux-gnu-gcc-10", # copy from your default configuration
      "CXX": "/bin/x86_64-linux-gnu-g++-10" # copy from your default configuration
    },
    "environmentSetupScript": "<path to your activation script>"
  }
```

Now the CMake extension will know to activate your conda environment before trying to build your project.

Open command palette one more time and go to `C/C++: Edit Configurations (UI)`. This should open a file called `c_cpp_properties.json` in the `.vscode` folder. At the start of the file add an environment section with the path to your conda environement, like this: 
```json
{
    "env": {
        "conda.prefix": "/Users/abroome/opt/miniconda3/envs/srg_uhd_radar"
    },
    "configurations:" [
      ...
    ]
}
```

Then find "include path" in the configuration section and add these two lines:

    ${env:conda.prefix}/include
    ${env:conda.prefix}/lib

Save it and you should be done.

If you don't know the path to your conda environment, you can find it by typing this in a terminal (with your conda environment activated):

`echo $CONDA_PREFIX`

The `conda.prefix` line should be all you need to change if you are switching between different conda environments. 

## Specify a Build Directory
To specify the source file location and build location for your cmake outputs, your `.vscode/settings.json` file would look something like this: 

```json
{
    "cmake.sourceDirectory": "${workspaceFolder}/sdr",
    "cmake.buildDirectory": "${workspaceFolder}/sdr/build",
    "files.associations": {
        "__threading_support": "cpp"
    }
}
```
