# catalog

A plugin of [mux](https://github.com/j4niwzis/mux).

Settings' Plugins page: every plugin built in, in their order, with what each does, its licence and whether it is mux's own or a third party's; and a page for each, with its repository, its licence and its README.

What a plugin says of itself comes from its `mux_plugin()` call (`SUMMARY`, `REPO`, `LICENSE`) and the `README.md` in its folder, put in the program whole (`mux.plugins.info`).

## Third-party plugins

A third party's plugin is a folder anywhere with a `CMakeLists.txt` calling `mux_plugin()` exactly as mux's own do, its name the folder's. It must say its `REPO` and `LICENSE`. Build mux with

    cmake -DMUX_EXTRA_PLUGINS="/path/to/one;/path/to/another" ...

and it is built and turned on like the rest (`MUX_PLUGIN_<NAME>`), and listed here.

Licence: AGPL-3.0-only, as mux.
