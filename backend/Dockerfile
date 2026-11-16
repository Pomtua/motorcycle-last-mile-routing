# syntax=docker/dockerfile:1.7
FROM ubuntu:22.04 AS engine
ARG BUILD_JOBS=4
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      ca-certificates cmake curl g++-12 make mold \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /engine
COPY engine/CMakeLists.txt ./
COPY engine/include ./include
COPY engine/src ./src
COPY engine/apps ./apps
COPY engine/scripts ./scripts
RUN bash scripts/setup_ortools.sh
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWITH_ORTOOLS=ON -DBUILD_TESTING=OFF \
    && cmake --build build --target solve --parallel "${BUILD_JOBS}"

FROM node:22.20.0-bookworm-slim AS api
WORKDIR /app
RUN corepack enable
COPY backend/package.json backend/pnpm-lock.yaml ./
RUN pnpm install --frozen-lockfile
COPY backend/tsconfig.json backend/tsconfig.build.json ./
COPY backend/src ./src
RUN pnpm build && pnpm prune --prod

FROM ubuntu:22.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ca-certificates libstdc++6 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=api /usr/local/bin/node /usr/local/bin/node
COPY --from=engine /engine/third_party/ortools/lib /engine/third_party/ortools/lib
COPY --from=engine /engine/build/solve /engine/build/solve
WORKDIR /app/backend
COPY --from=api /app/node_modules ./node_modules
COPY --from=api /app/dist ./dist
COPY backend/package.json ./
COPY backend/migrations ./migrations
ENV NODE_ENV=production \
    PORT=4000 \
    ENGINE_PATH=/engine/build/solve \
    MIGRATIONS_DIR=/app/backend/migrations \
    RESULTS_DIR=/app/results
EXPOSE 4000
CMD ["node", "dist/main.js"]
