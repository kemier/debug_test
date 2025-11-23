FROM ubuntu:22.04 AS base
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential gdb gdbserver socat \
    perl make wget ca-certificates

# Build OpenSSL from source with debug symbols (shared libraries)
WORKDIR /build/openssl
COPY openssl .
RUN ./Configure -d \
    linux-aarch64 \
    --prefix=/usr/local/openssl \
    --openssldir=/usr/local/openssl/ssl \
    && make -j$(nproc) \
    && make install

# Set environment for OpenSSL
ENV LD_LIBRARY_PATH=/usr/local/openssl/lib:$LD_LIBRARY_PATH
ENV PKG_CONFIG_PATH=/usr/local/openssl/lib/pkgconfig:$PKG_CONFIG_PATH
ENV PATH=/usr/local/openssl/bin:$PATH

# Build the application
WORKDIR /src
COPY . .
# Force GCC to record /src paths in debug symbols (regardless of actual build path)
# This ensures debug symbols always use /src prefix
# Link dynamically against custom OpenSSL with debug symbols
RUN gcc -O0 -g -Wall -fdebug-prefix-map=$(pwd)=/src \
    -I/usr/local/openssl/include \
    -L/usr/local/openssl/lib \
    -o app main.c -lssl -lcrypto -lpthread -ldl
# client image
FROM base AS client
CMD ["gdbserver","--multi",":7777","/src/app"]