# gluezilla-templater

Tool finding memory templates to be used by [gluezilla](https://github.com/COMET-DEPS/gluezilla).
Performs rowhammering to induce bit flips in DRAM and stores their physical address and parameters to reproduce them in a database file.
Inspired by
Google's [rowhammer-test](https://github.com/google/rowhammer-test),
[Linux Kernel (x86-64) - Rowhammer Privilege Escalation](https://www.exploit-db.com/exploits/36310),
[TRRespass](https://github.com/vusec/trrespass), and
[Blacksmith](https://github.com/comsec-group/blacksmith).

This repository contains two tools:
  * `validator`: tries to reproduce bit flips found in a text file (using the format of Google's rowhammer_test_ext)
  * `tester`: tries to find bit flips based on several parameters (see `include/config.h` and `default-config.ini`)


## Prerequisites

Tested with Ubuntu 20.04, CMake 3.16.3, GCC 11.1.0, Clang 12.0.1, Doxygen 1.8.17, and SQLite 3.31.1.  
Setup on Ubuntu (Clang, Doxygen (requires Graphviz), and SQLite are optional):

```bash
sudo apt update && sudo apt install cmake g++ dmidecode clang clang-format doxygen graphviz sqlite3 libsqlite3-dev
```

Note that the documentation is only generated while building when Doxygen is installed.
The database backend is only compiled if SQLite is installed.

If Blacksmith patterns should be supported, the [AsmJit library](https://github.com/asmjit/asmjit.git) is needed.
Initialize it's submodule with the following command.
Keep in mind to also enable `USE_ASMJIT` as described in the [Building section](#building).
See also the [Blacksmith section](#blacksmith).

```bash
git submodule update --init --recursive
```

In case a dmidecode version supporting the SMBIOS version is not yet available in the apt repositories: 

```bash
git clone https://git.savannah.nongnu.org/git/dmidecode.git
cd dmidecode
sudo make install
```


## Building

Several build options (e.g., logging and database backend) can be changed in the CMake cache before building, see [CMakeLists.txt](CMakeLists.txt).

### Manual

Use CMake to build the project:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

Note that you can change options in CMake cache by using the ```-D``` option.
For example, you can set the `USE_ASMJIT` variable to enable support for Blacksmith patterns like this:

```bash
cd build
cmake -D USE_ASMJIT=ON ..
```

### Visual Studio Code

When using Visual Studio Code, install [C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools), [Doxygen Documentation Generator](https://marketplace.visualstudio.com/items?itemName=cschlosser.doxdocgen), and [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools).

Once CMake Tools configured the project, F7 builds the project and generates the Doxygen documentation, Ctrl+F5 debugs the project and Shift+F5 starts it.  
Shift+Alt+F formats the source code according to `.clang-format` (choose C/C++ as default formatter).

You can use [Live Preview](https://marketplace.visualstudio.com/items?itemName=ms-vscode.live-server) to open the [Doxygen documentation](docs/html/index.html) in Visual Studio Code ("Show Preview" button in the top right corner).


## Configuration

A configuration file can be passed to the executables, the default file name is `config.ini`.
The `tester` tool can handle multiple configuration files as command line arguments but considers only the first configuration file to allocate memory and ignores the `[memory]` section of all other files.
For getting started, copy the default configuration file `default-config.ini` and rename it to `config.ini`.
Descriptions for all supported settings are available in `include/config.h` or in the [documentation](docs/html/classConfig.html).


## Database

When using Visual Studio Code, install [SQLite](https://marketplace.visualstudio.com/items?itemName=alexcvzz.vscode-sqlite) and [ERD Editor](https://marketplace.visualstudio.com/items?itemName=dineug.vuerd-vscode).

Right-clicking the database file (default: `data/<hostname>.db`) and selecting "Open Database" opens it in a SQLite Explorer at the bottom of the Explorer view.  
ERD Editor can be used to open `sql/tables.vuerd.json`, which contains an ER model of the database.

[SQLTools](https://marketplace.visualstudio.com/items?itemName=mtxr.sqltools) is a more advanced extensions that can handle SQLite databases (and many others). Install [SQLTools SQLite](https://marketplace.visualstudio.com/items?itemName=mtxr.sqltools-driver-sqlite) to install the SQLite driver as well as the basic extension. Next, a new connection to the database file must be configured.
If using [Remote - SSH](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-ssh), run 

```bash
sudo apt install npm
npm install sqlite3@4.2.0
```

and enable the Visual Studio Code setting `sqltools.useNodeRuntime` before configuring the connection.


## Blacksmith

When [Blacksmith](https://comsec.ethz.ch/research/dram/blacksmith/) patterns should be used, clone the [Blacksmith repository ](https://github.com/comsec-group/blacksmith.git) and build it (see Blacksmith's `README.md`).

Switch to your local Blacksmith repository and adapt `NUM_BANKS` in `include/GlobalDefines.hpp` to the number of DRAM banks in your system.
To start fuzzing for patterns, execute something like the following command inside the `build` directory:
```bash
sudo ./blacksmith --dimm-id 1 --runtime-limit 120000 --ranks 2
```

After the fuzzer finishes, you get two files in the `build` directory: `stdout.log` and `fuzz-summary.json`.

If the program does not log anything to `stdout.log`, it is probably stuck in an infinite loop determining the number of activations in one refresh window.
As a workaround, you can change `src/Memory/DramAnalyzer.cpp:140` to something like `for (size_t i = 0; i < 1000; i++)`.

Next, switch back to gluezilla-templater's repository and execute [parse_blacksmith.py](./py/parse_blacksmith.py):
```bash
./py/parse_blacksmith.py <path-to-blacksmith>/build/fuzz-summary.json --min_bitflips 100 --max_aggs 20
```
This will generate two files: `fuzz-summary.json_best_pattern.txt`, which contains the pattern with the highest count of bit flips, and `fuzz-summary.json_all_patterns.txt`, which contains all found patterns respecting the set command line options for minimum number of bit flips and maximum number of aggressors.

Set `hammer_algorithm=blacksmith` and copy the configuration to `config.ini`.
You can also test all patterns with [test_all_blacksmith_patterns.py](./py/test_all_blacksmith_patterns.py):
```bash
sudo ./py/test_all_blacksmith_patterns.py ./py/fuzz-summary.json_all_patterns.txt
```


## Temperature Controller

gluezilla-templater supports automatically changing the temperature of DRAM modules using a custom-built temperature controller, similar to the one described in the paper [A Deeper Look into RowHammer’s Sensitivities](https://arxiv.org/pdf/2110.10291).
Upon request, we can share the components of our temperature controller and its firmware.
