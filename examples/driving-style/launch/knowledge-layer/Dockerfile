FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
  build-essential \
  g++ \
  ca-certificates \
  git \
  wget \
  && rm -rf /var/lib/apt/lists/*

ARG CMAKE_VERSION=3.30.3
ARG TARGETARCH

RUN case "${TARGETARCH}" in \
  amd64) CMAKE_ARCH=linux-x86_64 ;; \
  arm64) CMAKE_ARCH=linux-aarch64 ;; \
  *) echo "Unsupported architecture: ${TARGETARCH}" && exit 1 ;; \
  esac \
  && wget -q https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-${CMAKE_ARCH}.sh \
  && chmod +x cmake-${CMAKE_VERSION}-${CMAKE_ARCH}.sh \
  && ./cmake-${CMAKE_VERSION}-${CMAKE_ARCH}.sh --skip-license --prefix=/usr/local \
  && rm cmake-${CMAKE_VERSION}-${CMAKE_ARCH}.sh

RUN wget https://archives.boost.io/release/1.87.0/source/boost_1_87_0.tar.gz \
  && tar xzf boost_1_87_0.tar.gz \
  && cd boost_1_87_0 \
  && ./bootstrap.sh \
  && ./b2 --with-system --with-filesystem --with-thread -j8 install

WORKDIR /src

COPY cdsp/knowledge-layer /src/cdsp/knowledge-layer
COPY examples/driving-style /src/examples/driving-style

WORKDIR /src/cdsp/knowledge-layer
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build --target driving_style_client -j8

ENTRYPOINT ["/src/cdsp/knowledge-layer/build/bin/driving_style_client", "/src/examples/driving-style/model/driving_style_reasoning.json"]
