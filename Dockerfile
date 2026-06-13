# C-DSL development environment
# Build:   docker build -t cdsl-dev .
# Run:     docker run -it --rm -v $(pwd):/cdsl cdsl-dev
# Release: docker build --target release -t cdsl-runtime .

FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc \
    g++ \
    cmake \
    flex \
    bison \
    libcurl4-openssl-dev \
    python3 \
    git \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DCDSL_GENERATE_DOCS=OFF \
    && cmake --build build -j$(nproc)

FROM ubuntu:24.04 AS release
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libcurl4 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/build/libcdsl*.so* /usr/local/lib/
COPY --from=builder /build/build/cdsl_demo /usr/local/bin/
COPY --from=builder /build/build/cdsl_official_review /usr/local/bin/

RUN ldconfig

RUN useradd -m cdsl
USER cdsl

CMD ["/bin/bash"]

FROM ubuntu:24.04 AS dev
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc \
    g++ \
    clang \
    cmake \
    flex \
    bison \
    libcurl4-openssl-dev \
    doxygen \
    graphviz \
    clang-format \
    clang-tidy \
    python3 \
    git \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -m cdsl
USER cdsl

WORKDIR /cdsl
CMD ["/bin/bash"]
