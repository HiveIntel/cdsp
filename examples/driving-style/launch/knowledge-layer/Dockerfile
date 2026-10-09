# syntax=docker/dockerfile:1.7
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
  build-essential \
  g++ \
  ca-certificates \
  ccache \
  git \
  libserd-dev \
  nlohmann-json3-dev \
  pkg-config \
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
RUN --mount=type=cache,id=driving-style-cmake-${TARGETARCH},target=/build-cache \
    --mount=type=cache,id=driving-style-ccache-${TARGETARCH},target=/root/.cache/ccache \
    cmake -S . -B /build-cache \
      -DCMAKE_BUILD_TYPE=Release \
      -DKL_BUILD_TESTS=OFF \
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  && cmake --build /build-cache --target driving_style_client -j"$(nproc)" \
  && install -D -m 0755 /build-cache/bin/driving_style_client /opt/driving-style/driving_style_client

ENV MODEL_CONFIG=/src/examples/driving-style/model/driving_style_reasoning.json

ENTRYPOINT ["/opt/driving-style/driving_style_client"]
