# UCXX

UCXX is an object-oriented C++ interface for UCX, with native support for Python bindings.

## Building

### Convenience Script

For convenience, we provide the `./build.sh` script. By default, it will build and install both C++ and Python libraries. For a detailed description on available options please check `./build.sh --help`.

Building C++ and Python libraries manually is also possible, see instructions on building [C++](#c) and [Python](#python).

Additionally, there is a `./build_and_run.sh` script that will call `./build.sh` to build everything as well as running C++ and Python tests and a few benchmarks. Similarly, details on existing options can be queried with `./build_and_run.sh`.

### C++

To build and install C++ library to `${CONDA_PREFIX}`, with both Python and RMM support, as well as building all tests run:

```
mkdir cpp/build
cd cpp/build
cmake .. -DCMAKE_INSTALL_PREFIX=${CONDA_PREFIX} \
      -DBUILD_TESTS=ON \
      -DCMAKE_BUILD_TYPE=Release \
      -DUCXX_ENABLE_PYTHON=ON \
      -DUCXX_ENABLE_RMM=ON
make -j install
```

### Python

```
cd python
python setup.py install
```

## Running benchmarks

### C++

TBD

### Python

Benchmarks are available for both the Python "core" (synchronous) API and the "high-level" (asynchronous) API.

#### Synchronous

```python
# Thread progress without delayed notification NumPy transfer, 100 iterations
# of single buffer with 100 bytes
python python/benchmarks/send-recv.py \
    -o numpy \
    --n-iter 100 \
    --n-bytes 100

# Blocking progress without delayed notification RMM transfer between GPUs 0
# and 3, 100 iterations of 2 buffers (using multi-buffer interface) each with
# 1 MiB
python python/benchmarks/send-recv.py \
    -o rmm \
    --server-dev 0 \
    --client-dev 3 \
    --n-iter 100 \
    --n-bytes 100 \
    --progress-mode blocking
```

#### Asynchronous

```python
# NumPy transfer, 100 iterations of 8 buffers (using multi-buffer interface)
# each with 100 bytes
python python/benchmarks/send-recv-async.py \
    -o numpy \
    --n-iter 100 \
    --n-bytes 100 \
    -x 8

# RMM transfer between GPUs 0 and 3, 100 iterations of 2 buffers (using
# multi-buffer interface) each with 1 MiB
python python/benchmarks/send-recv-async.py \
    -o rmm \
    --server-dev 0 \
    --client-dev 3 \
    --n-iter 100 \
    --n-bytes 1MiB \
    -x 2

# Non-blocking progress without delayed notification NumPy transfer,
# 100 iterations of single buffer with 1 MiB
UCXPY_ENABLE_DELAYED_SUBMISSION=0 UCXPY_PROGRESS_MODE=non-blocking \
    python python/benchmarks/send-recv-async.py \
    -o numpy \
    --n-iter 100 \
    --n-bytes 1MiB
```

## Logging

Logging is independently available for both C++ and Python APIs. Since the Python interface uses the C++ backend, C++ logging can be enabled when running Python code as well.

### C++

The C++ interface reuses the UCX logger and provides the same log levels and can be enabled via the `UCXX_LOG_LEVEL` environment variable. However, it will not enable UCX logging, one must still set `UCX_LOG_LEVEL` for UCX logging. A few examples are below:

```
# Request trace log level
UCXX_LOG_LEVEL=TRACE_REQ

# Debug log level
UCXX_LOG_LEVEL=DEBUG
```

### Python

The UCXX Python interface uses the `logging` library included in Python. The only used levels currently are `INFO` and `DEBUG`, and can be enabled via the `UCXPY_LOG_LEVEL` environment variable. A few examples are below:

```
# Enable Python info log level
UCXPY_LOG_LEVEL=INFO

# Enable Python debug log level, UCXX request trace log level and UCX data log level
UCXPY_LOG_LEVEL=DEBUG UCXX_LOG_LEVEL=TRACE_REQ UCX_LOG_LEVEL=DATA
```
