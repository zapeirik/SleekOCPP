# Headless SleekOCPP on Linux (no GUI, no Qt Widgets).
#
#   docker build -t sleekocpp .
#   docker run --rm -p 9000:9000 -p 8080:8080 -v sleekocpp-data:/data sleekocpp
#
# Chargers connect to ws://<docker host>:9000/<charge point id>, the REST API is on http://<docker host>:8080.

FROM debian:trixie AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ca-certificates \
        qt6-base-dev qt6-websockets-dev qt6-httpserver-dev libqt6sql6-sqlite \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src
COPY tests ./tests
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSLEEKOCPP_BUILD_GUI=OFF -DSLEEKOCPP_BUILD_TESTS=ON \
    && cmake --build build -j"$(nproc)" \
    && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure

FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        libqt6core6t64 libqt6network6 libqt6websockets6 libqt6httpserver6 libqt6sql6 libqt6sql6-sqlite \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/src/sleekocpp /usr/local/bin/sleekocpp
VOLUME /data
EXPOSE 9000 8080
# The REST API has no authentication: only publish port 8080 on networks you trust.
ENTRYPOINT ["sleekocpp", "--headless", "--db", "/data/sleekocpp.db", "--rest-bind", "0.0.0.0"]
