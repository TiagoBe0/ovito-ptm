# Installing PyTorch with Conda and Compiling OVITO on Linux

This guide walks you through setting up a Conda environment with PyTorch and
compiling OVITO with LibTorch (PyTorch C++) support enabled on Linux.

---

## Prerequisites

Make sure the following are installed on your system before starting:

- **Git**
- **CMake >= 3.25** — check with `cmake --version`
- **Ninja** (recommended build backend) — `sudo apt install ninja-build`
- **GCC >= 12** or Clang with C++23 support — check with `gcc --version`
- **Qt 6.3+** development libraries
- **Conda** (Miniconda or Anaconda)

On Ubuntu/Debian, install the system dependencies:

```bash
sudo apt update
sudo apt install -y build-essential ninja-build git cmake \
    qt6-base-dev qt6-base-private-dev libboost-dev zlib1g-dev
```

---

## Step 1 — Create a Conda Environment

Create a dedicated environment with Python 3.11 (or 3.10/3.12 also work):

```bash
conda create -n ovito-build python=3.11 -y
conda activate ovito-build
```

---

## Step 2 — Install PyTorch via Conda

Install PyTorch **CPU-only** (use this if you do not have an NVIDIA GPU or if
you just want to compile and run inference on the CPU):

```bash
conda install pytorch cpuonly -c pytorch -y
```

If you have an NVIDIA GPU and want CUDA support, replace the command above with
the appropriate version from [pytorch.org](https://pytorch.org/get-started/locally/).
Example for CUDA 12.1:

```bash
conda install pytorch pytorch-cuda=12.1 -c pytorch -c nvidia -y
```

Verify the installation:

```bash
python -c "import torch; print(torch.__version__, torch.cuda.is_available())"
```

---

## Step 3 — Find the LibTorch Root Path

OVITO's CMake system needs to find the LibTorch C++ headers and `.cmake` config
files that are bundled with the Python `torch` package.

Run this command to get the exact path:

```bash
python -c "import torch, os; print(os.path.dirname(torch.__file__))"
```

It will print something like:

```
/home/youruser/miniconda3/envs/ovito-build/lib/python3.11/site-packages/torch
```

Save this path — it is your **`LIBTORCH_ROOT`**. Use it in the CMake step below.

You can also set it as a shell variable for convenience:

```bash
export LIBTORCH_ROOT=$(python -c "import torch, os; print(os.path.dirname(torch.__file__))")
echo $LIBTORCH_ROOT
```

---

## Step 4 — Clone the Repository

```bash
git clone https://github.com/tiagobe0/ovito-ptm.git
cd ovito-ptm
```

Or, if you already have the repository:

```bash
cd ovito-ptm
git checkout claude/pytorch-ovito-install-guide-akTdW
```

---

## Step 5 — Configure the Build with CMake

Create a build directory and configure CMake with LibTorch enabled.
The key flags are:

| CMake Flag | Purpose |
|---|---|
| `-DOVITO_USE_LIBTORCH=ON` | Enable LibTorch/PyTorch C++ support |
| `-DOVITO_LIBTORCH_ROOT=...` | Path to the `torch` package folder |
| `-DOVITO_BUILD_PLUGIN_ML=ON` | Build the ML plugin (on by default) |

```bash
mkdir build && cd build

cmake .. \
  -GNinja \
  -DCMAKE_BUILD_TYPE=Release \
  -DOVITO_USE_LIBTORCH=ON \
  -DOVITO_LIBTORCH_ROOT="${LIBTORCH_ROOT}" \
  -DOVITO_BUILD_APP=ON \
  -DOVITO_BUILD_PLUGIN_ML=ON
```

If CMake finds LibTorch successfully you will see a line like:

```
-- LibTorch found: 2.x.x
```

> **Troubleshooting:** If CMake reports that `TorchConfig.cmake` was not found,
> double-check the value of `LIBTORCH_ROOT`. The directory must contain
> `share/cmake/Torch/TorchConfig.cmake`. You can verify with:
> ```bash
> ls "${LIBTORCH_ROOT}/share/cmake/Torch/TorchConfig.cmake"
> ```
> As an alternative you can pass the path directly:
> ```bash
> -DTorch_DIR="${LIBTORCH_ROOT}/share/cmake/Torch"
> ```

---

## Step 6 — Compile

```bash
ninja -j$(nproc)
```

The build will take several minutes. After it completes, the OVITO binaries will
be inside the `build/` directory.

---

## Step 7 — Run OVITO

```bash
./build/bin/ovito
```

If you get a library loading error about `libtorch*.so` not found at runtime,
add the LibTorch library directory to `LD_LIBRARY_PATH`:

```bash
export LD_LIBRARY_PATH="${LIBTORCH_ROOT}/lib:${LD_LIBRARY_PATH}"
./build/bin/ovito
```

You can add this export to your `~/.bashrc` or `~/.profile` to make it permanent.

---

## Complete Example (Copy-Paste)

```bash
# 1. Activate the conda environment
conda activate ovito-build

# 2. Get the LibTorch root path
export LIBTORCH_ROOT=$(python -c "import torch, os; print(os.path.dirname(torch.__file__))")

# 3. Go to the repo and create the build dir
cd ovito-ptm
mkdir -p build && cd build

# 4. Configure
cmake .. \
  -GNinja \
  -DCMAKE_BUILD_TYPE=Release \
  -DOVITO_USE_LIBTORCH=ON \
  -DOVITO_LIBTORCH_ROOT="${LIBTORCH_ROOT}" \
  -DOVITO_BUILD_PLUGIN_ML=ON

# 5. Build
ninja -j$(nproc)

# 6. Run
export LD_LIBRARY_PATH="${LIBTORCH_ROOT}/lib:${LD_LIBRARY_PATH}"
./bin/ovito
```

---

## Common Errors

### `TorchConfig.cmake` not found
The `LIBTORCH_ROOT` path is wrong or the conda environment is not activated.
Re-run `conda activate ovito-build` and re-export `LIBTORCH_ROOT`.

### ABI mismatch / unresolved `torch::jit` or `at::` symbols
This happens when PyTorch was built with a different C++ ABI than your compiler.
LibTorch installed via conda typically uses `_GLIBCXX_USE_CXX11_ABI=1`.
The CMake configuration handles this automatically via `TORCH_CXX_FLAGS` — make
sure you are not overriding those flags manually.

### `libgomp` or `libc10` not found at runtime
Add the LibTorch `lib/` directory to `LD_LIBRARY_PATH` as shown in Step 7.

### Wrong Python version
Make sure the conda environment is activated **before** running CMake so that
`python -c "import torch..."` resolves to the correct installation.
