# Multi-stage build for FuelFlux Controller
FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    g++ \
    make \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source code
COPY . .

# Create build directory and build the project
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

# Runtime stage
FROM ubuntu:22.04 AS runtime

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN useradd -m -s /bin/bash fuelflux

# Set working directory
WORKDIR /app

# Copy built executable from builder stage
COPY --from=builder /app/build/bin/fuelflux /app/

# Change ownership to non-root user
RUN chown -R fuelflux:fuelflux /app

# Switch to non-root user
USER fuelflux

# Expose any ports if needed (none for console app)
# EXPOSE 8080

# Set environment variables
ENV FUELFLUX_CONTROLLER_ID=docker-controller-001
ENV FUELFLUX_LOG_LEVEL=INFO

# Run the application
CMD ["./fuelflux"]
