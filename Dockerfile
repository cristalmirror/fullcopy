# Build environment with the MinGW-w64 cross compiler from the latest Fedora.
FROM fedora:latest

RUN dnf install -y \
        make \
        mingw64-gcc-c++ \
        mingw64-winpthreads-static \
    && dnf clean all

WORKDIR /project

CMD ["make"]
