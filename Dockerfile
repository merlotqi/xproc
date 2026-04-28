# Reproducible Linux toolchain for local builds and CI (matches Ubuntu 22.04–style images).
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    ca-certificates \
    pkg-config \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

CMD ["bash"]
