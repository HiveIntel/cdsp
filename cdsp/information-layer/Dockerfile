FROM node:22.5.1

WORKDIR /app

COPY package.json .
COPY tsconfig.json .
COPY handlers ./handlers
COPY utils ./utils
COPY router ./router

# Install dependencies, including workspaces
RUN npm install

# FIXME: Ensure Apache IoTDB client source is present (workaround install)
# Note: This is a temporary workaround to ensure the Apache IoTDB client is available. Dependency management should be properly handled in the future.
RUN npm install git+https://github.com/apache/iotdb-client-nodejs.git#2bd256f97077b0c5a86adb0e96cdc7d9097ba432

# FIXME: Build @iotdb/client artifacts (workaround)
# Note: This is a temporary workaround to ensure the Apache IoTDB client artifacts are built. Proper build scripts should be implemented in the future.
# Build @iotdb/client workaround artifacts
RUN cd node_modules/@iotdb/client \
    && npm install \
    && npm run build

# Transpile TypeScript to JavaScript
RUN npm run build

# Remove devDependencies to reduce the final image size
RUN npm prune --omit=dev

# Expose the necessary port
EXPOSE 8080

# Command to run the WebSocket server
CMD ["node", "./dist/router/src/websocket-server.js"]