FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Install only runtime dependencies
RUN apt-get update && apt-get install -y \
    libfmt-dev \
    libyaml-cpp-dev \
    libspdlog-dev \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Create app directory
RUN mkdir -p /app/logs

WORKDIR /app

# Copy binaries and config
COPY ./config.yaml .
COPY ./build/client .
COPY ./build/server .

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

CMD ["./server"]