# Development environment for C-DSL
# Build: docker build -t cdsl-dev .
# Run:   docker run -it --rm -v $(pwd):/cdsl cdsl-dev

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    g++ \
    clang \
    cmake \
    flex \
    bison \
    libcurl4-openssl-dev \
    libreofficekit-dev \
    doxygen \
    graphviz \
    clang-format \
    clang-tidy \
    python3 \
    python3-pip \
    git \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /cdsl

CMD ["/bin/bash"]
