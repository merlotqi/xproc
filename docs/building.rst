Building and installing
=======================

Prerequisites
-------------

* C++17 compiler
* CMake 3.14 or later (README currently mentions 3.16+ for contributors; the project minimum is 3.14)
* Optional: nlohmann/json, Protocol Buffers (for reference codecs)

Basic configure and build
-------------------------

.. code-block:: bash

   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build

Static vs shared library
------------------------

By default ``xproc`` is a **static** library. For a shared library:

.. code-block:: bash

   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DXPROC_BUILD_SHARED=ON
   cmake --build build

On **Windows**, with ``XPROC_BUILD_SHARED=ON``, CMake puts ``xproc.dll`` and all in-tree
executables (tests, examples, benchmarks) in the same per-configuration directory (e.g.
``build/Debug`` or ``build/Release``). That avoids ``0xc0000135`` (STATUS_DLL_NOT_FOUND)
during ``gtest_discover_tests`` at build time. If your ``build`` tree was configured before
this layout existed, delete the build directory and re-run CMake configure.

Install and pkg-config
----------------------

.. code-block:: bash

   cmake --install build --prefix /usr/local

This installs headers, the library, the CMake package, and ``lib/pkgconfig/xproc.pc``.

.. code-block:: bash

   export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
   pkg-config --cflags --libs xproc

On Linux, ``Libs.private`` includes ``-pthread`` and ``-lrt`` where applicable so ``pkg-config --libs --static xproc`` can link a static ``libxproc.a``. Optional JSON/Protobuf builds are not fully reflected in ``xproc.pc``; use ``find_package(xproc)`` or add those dependencies manually.

CMake consumers
---------------

.. code-block:: cmake

   find_package(xproc CONFIG REQUIRED)
   target_link_libraries(my_target PRIVATE xproc::xproc)

If xproc was built with JSON or Protobuf enabled, the generated ``xprocConfig.cmake`` runs ``find_dependency`` for those packages.

Docker
------

The repository root ``Dockerfile`` provides Ubuntu 22.04 with GCC, CMake, Ninja, and pkg-config. Example:

.. code-block:: bash

   docker build -t xproc:dev .
   docker run --rm -it -v "$(pwd):/workspace" -w /workspace xproc:dev bash

Optional CMake flags
--------------------

.. code-block:: bash

   cmake -S . -B build -DXPROC_WITH_NLOHMANN_JSON=ON
   cmake -S . -B build -DXPROC_WITH_PROTOBUF=ON
   cmake -S . -B build -DXPROC_BUILD_TESTS=ON -DXPROC_BUILD_EXAMPLES=ON
   cmake -S . -B build -DXPROC_BUILD_BENCHMARKS=ON

Benchmarks are split into separate executables (``xproc_bench_*``). To run all labeled CTest entries:

.. code-block:: bash

   cmake --build build --target xproc_run_benchmarks

Phase 1 regression suite
------------------------

For the focused shared-memory builder / manifest / mismatch regression gate:

.. code-block:: bash

   cmake --build build --target xproc_run_tests

Sphinx documentation (this tree)
--------------------------------

From the ``docs/`` directory:

.. code-block:: bash

   pip install -r requirements.txt
   sphinx-build -b html . _build/html

Open ``_build/html/index.html`` in a browser.
