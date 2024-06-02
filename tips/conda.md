Intro tutorial: https://conda.io/projects/conda/en/latest/user-guide/getting-started.html

The very useful page on managing conda environments: https://conda.io/projects/conda/en/latest/user-guide/tasks/manage-environments.html

As a side note, there are a couple of configuration changes that we recommend considering:
1. By default, conda will automatically open up the "base" environment every time you open a terminal window. You can disable this if you prefer: `conda config --set auto_activate_base false`
2. Conda uses "channels" to manage where you get software packages from. There are tons of channels out there, but there are basically only two you need to know about: the default set and `conda-forge`. The default set is a curated set of packaged that's supported and maintained as part of the Anaconda commercial service. `conda-forge` is a community-supported much larger repository of packages. In our case, the up-to-date UHD software we need is only available in `conda-forge`. You may wish to prioritize using packages from `conda-forge` when they are available, in which case you can set: `conda config --add channels conda-forge`

(The `environment.yaml` file provided in this repo internally adds `conda-forge` as the default channel only for the environment it creates.)