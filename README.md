# Stanford Radio Glaciology USRP Radar

This repository will (eventually) contain a unified set of code for all of Stanford Radio Glaciology's USRP-based radar instruments.

## Repository Organization

* `config/` contains YAML files encapsulating all of the settings needed to run various experiments on different hardware setups. Ideally, the same code should be able to be run for every SDR and every type of measurement with a different YAML file from this folder determining all of the necessary settings and parameters.
* `data/` is where you can locally store whatever results/outputs your experiment creates. Everything (except the readme) in this folder will be ignored by git. Please don't check your specific results into version control (but do back them up somewhere).
* `preprocessing/` contains any scripts that need to run BEFORE the SDR code but that don't directly interface with the SDR. (For example: generation of a chirp waveform.)
* `sdr/` contains any code that directly controls the SDR
* `postprocessing/` contains any code that processing or plots data recorded by the SDR without directly interfacing with the SDR.
* `delay_line/` still exists for mostly historical reasons. It's a stand-alone setup for configuring an SDR to act as a virtual delay line. We'll probably remove it from this repository at some point.
* `run_default.sh` is a helper script that runs a sequence of commands for a basic radar experiment. Each experiment may eventually have its own, but, for now, this is just intended as sort of a starter guide to what you need to run.

## Configuring your environment

The easiest way to make sure you have the right dependencies to run everything here is to use conda. If you're new to conda, there are some notes on setting things up [here](conda.md).

Dependencies for this project are managed through the `environment.yaml` file. You can create a conda environment with everything you need to run this code like this:

`conda env create -n myenvironmentname -f environment.yaml`

Then activate it like this:

`conda activate myenvironmentname`

And you're good to go. This install UHD and all the other necessary dependencies.

### Adding a dependency

If you need to add a new package, you can update `environment.yaml`. It's probably easiest to just do this manually, however, if you really want, you can also update it by exporting your existing environment:

`conda env export --from-history > environment.yaml`

Please check that the changes you made are what you expected before committing them. Also, please do not do this without the `--from-history` command. The full environment export is a nightmare to debug across multiple platforms. When in doubt, manually update it. And always test it out by using the new `environment.yaml` to create a new environment.

### Using Visual Studio Code

It takes a few extra steps to tell Visual Studio Code that you're using the conda environment. For setup instructions, [see here](vscode.md).

### Running the code

In most cases, all the necessary parts of the workflow can be done together by running the `run_default.sh` script.

Any arguments passed to the script will be passed along to each of the three component pieces (pre-processing, SDR code, and post-processing). In particular, all scripts accept a path to a configuration YAML. You can run an experiment defined by a particular YAML file like this:

```
./run_default.sh config/my_experiment.yaml
```

Another handy trick is saving the output to a file. You can do that like this:

```
./run_default.sh 2>&1 | tee log.txt
```

## Adding features, git conventions

The basic workflow for adding features in should be something like this:

1. Create your own branch of this repository: `git checkout -b thomas/cool-new-feature`
   
   The `name/` thing is just a convention, but it's a nice way of who is working on what. Also, many git GUIs (such as Sublime Merge) will sort different branches into folders so you can easily see all of your (or someone else's) branches.

2. Prototype whatever changes you want to make. Do whatever it takes to make it work.

3. (Optional) You might want to commit these changes (`git add <changed files>`, `git commit -m "what I did"`) and then push the changes to GitHub (`git push --set-upstream origin thomas/cool-new-feature`). This will push your changes to GitHub, still in a separate branch, allowing others to see your branch along with the `main` branch and all the others.

3. Figure out how to cleanly integrate whatever your doing with the rest of the code. This will probably mean making it possible to configure your feature or disable it completley with only changes to a YAML config file. Then make sure that the defaults are to not use it (if the feature won't be needed by others), so your changes don't break someone else's instrument.

4. Commit again. Push your changes. (See #3.)

5. Go to GitHub and you'll be prompted to create a [pull request](https://docs.github.com/en/github/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/about-pull-requests). Pull requests are a GitHub feature that lets you and others preview what happens if you merge your branch into the `main` branch. You can send others a link so we can all review the changes you're propsoing, including making comments on them and discussing if there are any issues.