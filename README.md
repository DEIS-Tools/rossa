# Generating Rotor Models and Verifying them in UPPAAL

This project installs as a Python package. It can then either be imported from other scripts or run on its own.

## Setup

To setup (optionally) install a Python 3.9+ environment and activate it.

```console
python3 -m venv venv
source venv/bin/activate
```

Install the package and its requirements.

- Use `pip install .` to do a normal Python package install.
- Use `pip install -e .` to install it in "editable" mode (source changes to the package are effective immediately without reinstall)

```console
pip install -e .
```

## Running

Set the UPPAAL license key in the `UPPAAL_KEY` environment variable or pass it on the command line with the `--uppaal-key` flag.

Just run `rossa --help` to see commands. Can run help on the subcommands too `rossa generate --help`.

To generate example file: 

```console
rossa generate -c example/config_sampling.toml --model-type=sampling -o my-uppaal-file.xml --ext-name=libmyextension.so
```

## Building

Ensure `build` package is installed.

Run `python -m build`. Will place build outputs, both the source directory, and a Python wheel in `./dist`.

# Details

## Generation

After the configuration file for model generation is read there are possible overwrites controlled by the parameters `--si` (set integer), `--sb` (set boolean), `--ss` (set string). Be sure to specify the key proper of overriding, e.g. `--si model.num_nodes=12`.

The `--mode` parameter is important. The choice determines which queries are generated as well as some parameters for the templates inside `src/rossa/data`. These templates contain `<<PLACEHOLDERS>>` that have their content replaced by the model generation. For example the capacity of the ports are embedded thus:

```c
const packet_t PORT_CAPACITIES[port_t] = <<GEN_PORT_CAPACITIES>>;
```
