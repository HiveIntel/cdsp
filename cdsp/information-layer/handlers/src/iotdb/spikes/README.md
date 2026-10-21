# IoTDB Node Client Smoke Tests

This folder contains smoke tests and notes used to validate the Apache IoTDB Node.js client baseline.

## Context

During validation, we found packaging inconsistencies:

- Apache docs/repo reference `@iotdb/client`
- `@iotdb/client` was not directly installable from npm in our environment
- `iotdb-client-nodejs` (npm) was installable but not usable for our setup/export needs

Workaround used in this ticket:

1. Install from the Apache GitHub repo commit
2. Build the package locally in `node_modules/@iotdb/client`
3. Validate import + session open/query/close via smoke tests

Upstream tracking issue:

- https://github.com/apache/iotdb-client-nodejs/issues/9

---

## Current working setup

Install the dependency from the GitHub commit (from `cdsp/information-layer`):

```bash
npm install git+https://github.com/apache/iotdb-client-nodejs.git#2bd256f97077b0c5a86adb0e96cdc7d9097ba432
```

Then build the dependency artifacts:

```bash
cd node_modules/@iotdb/client
npm install
npm run build
```

> Note: this is a workaround. A clean published package flow is preferred long-term.

---

## Smoke tests

### 1) Import smoke

File:

- `handlers/src/iotdb/spikes/iotdb-import-smoke.mjs`

Run:

```bash
node handlers/src/iotdb/spikes/iotdb-import-smoke.mjs
```

Expected:

- `@iotdb/client` imports successfully
- export list includes `Session`

### 2) Session/query smoke

File:

- `handlers/src/iotdb/spikes/iotdb-client-smoke.ts`

Run:

```bash
npx ts-node handlers/src/iotdb/spikes/iotdb-client-smoke.ts
```

Expected:

- session opens
- `SHOW DATABASES` executes
- session closes successfully

---

## Environment variables

Defaults used if not provided:

- `IOTDB_HOST=localhost`
- `IOTDB_PORT=6667`
- `IOTDB_USER=root`
- `IOTDB_PASSWORD=root`

Ensure a local IoTDB instance is running before testing.
