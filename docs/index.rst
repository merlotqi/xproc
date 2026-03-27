xproc documentation
=====================

.. image:: https://img.shields.io/badge/C%2B%2B-17-blue.svg
   :alt: C++17

**xproc** is a high-performance **single-producer single-consumer (SPSC)** IPC library built on shared-memory ring buffers. It supports **fixed-length** and **variable-length** messages on **Linux** (POSIX shm + futex) and **Windows** (named file mapping; ``atomic_wait`` uses **polling with backoff**, not ``WaitOnAddress``, so wake semantics match across separate ``MapViewOfFile`` views—see :doc:`platforms`).

.. toctree::
   :maxdepth: 2
   :caption: Contents

   overview
   quickstart
   building
   architecture
   platforms
   advanced
   design

When built with Sphinx, use the sidebar search or the generated index pages.
