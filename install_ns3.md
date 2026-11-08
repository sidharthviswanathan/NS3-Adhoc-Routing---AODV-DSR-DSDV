# Installing ns-3.49 on Debian 13 (command line)

Builds ns-3.49 from source in `~/NS3`, using the `optimized` profile, with
examples, tests and Python bindings enabled.

## 1. Install system packages

```bash
sudo apt update
sudo apt install -y git python3 python3-pip python3-venv python3-dev \
    g++ make cmake ninja-build ccache pkg-config bzip2 wget
```

Optional libraries, which turn on extra ns-3 features:

```bash
sudo apt install -y libgsl-dev libsqlite3-dev libxml2-dev libeigen3-dev
```

| Library | Used for |
| --- | --- |
| `libgsl-dev` | Some Wi-Fi and spectrum error models, and statistics |
| `libsqlite3-dev` | Saving results to a database |
| `libxml2-dev` | Loading and saving ConfigStore settings as XML |
| `libeigen3-dev` | Matrix-based features, such as MIMO models |

## 2. Download and extract ns-3

```bash
mkdir -p ~/NS3
cd ~/NS3
wget https://www.nsnam.org/releases/ns-3.49.tar.bz2
tar xjf ns-3.49.tar.bz2
```

The source is now in `~/NS3/ns-3.49`.

## 3. Create the Python virtual environment

ns-3.49 specifies `cppyy` 3.5.0 for its Python bindings. Skip this step if you
only plan to write simulations in C++, and also remove
`--enable-python-bindings` in step 4.

```bash
cd ~/NS3
python3 -m venv venv
source venv/bin/activate
pip install cppyy==3.5.0
```

## 4. Configure

Keep the venv active, so that CMake finds the Python that has `cppyy`.

```bash
cd ~/NS3/ns-3.49
./ns3 configure --build-profile=optimized --enable-examples --enable-tests --enable-python-bindings
```

Check these lines in the summary before building:

```text
Build profile                 : optimized
Python Bindings               : ON
```

If bindings show `OFF (missing dependency: cppyy)`, CMake is using the system
Python instead of the venv. Point it at the venv's Python explicitly:

```bash
cd ~/NS3/ns-3.49
./ns3 configure --build-profile=optimized --enable-examples --enable-tests --enable-python-bindings -- -DPython3_EXECUTABLE=$HOME/NS3/venv/bin/python3
```

## 5. Build

```bash
cmake --build cmake-cache -j2
```

Set `-j` to your CPU core count. On this 2-core machine, `./ns3 build -j2` still
runs only one job, because the script limits jobs to the core count minus 1.
Calling `cmake --build` directly avoids that limit.

The build can take an hour or more on 2 cores. Over SSH, run it inside `tmux` so
that a dropped connection doesn't stop it.

Lines marked `warning:` are harmless if the build keeps going, including
`-Warray-bounds` warnings from `vector<bool>::reserve` in the LTE schedulers. A
real failure shows `error:` and `FAILED:`, and the build stops.

## 6. Test the installation

```bash
./ns3 run first
echo "exit code: $?"

./ns3 run examples/tutorial/first.py
echo "exit code: $?"

./test.py -s core
```

The `optimized` profile turns logging off, so the two `first` programs print
little or nothing. An exit code of `0` means they ran successfully.

## Everyday use

Activate the venv in each new terminal if you're using the Python bindings:

```bash
source ~/NS3/venv/bin/activate
cd ~/NS3/ns-3.49
```

Put your simulation in `scratch/`, then run it by name:

```bash
./ns3 run my-sim                    # scratch/my-sim.cc
./ns3 run "my-sim --nPackets=10"    # with arguments your script defines
./ns3 run scratch/my-sim.py         # Python simulation
```

## Notes

- **Logging:** `NS_LOG` and asserts don't work in the `optimized` profile. Print
  results from your script, or use trace files, pcap files or FlowMonitor. For
  debugging, configure with `--build-profile=default` and rebuild.
- **`./ns3 install`:** not needed. Simulations run directly from
  `~/NS3/ns-3.49`. It's only for installing ns-3 so other projects can link
  against it.
- **NetAnim:** the plain `ns-3.49` package probably doesn't include the NetAnim
  animator app. You only need it to view animations in a GUI.
- **Plain `python`:** Debian only provides `python3` outside a venv. Inside an
  active venv, `python` works too.
