# IoTDB

This directory contains the IoTDB handler implementation for the Information Layer.

The handler now uses the **Node.js native IoTDB client** (`@iotdb/client`) as the only runtime path. Legacy Thrift-based runtime code has been removed.

## Current Architecture

### `IoTDBHandler`

- Main orchestrator for get/set/subscribe/unsubscribe requests.
- Owns a single shared `IoTDBSession` instance.
- Initializes schema and database readiness.
- Delegates subscription lifecycle operations to `SubscriptionSimulator`.

### `IoTDBSession`

- Encapsulates IoTDB session lifecycle (`open`, `close`).
- Executes query/write operations for:
  - `getDataPoints`
  - `getDataPointsInWindow` (polling window support)
  - `setDataPoints`
- Handles database creation via `createDatabaseIfNeeded`.
- Uses explicit SQL dialect setup (`TREE`) when possible and logs fallback if unsupported.

### `SubscriptionSimulator`

- Manages in-memory websocket subscriptions.
- Owns timer lifecycle (start/stop polling interval).
- Uses the **shared** `IoTDBSession` injected by `IoTDBHandler` (does not own/close the session).
- Polls IoTDB using time windows and sends updates to subscribed clients.

## Subscription Model

Node client support for native topic consumer APIs is limited for our target stack, so subscription behavior is currently implemented with polling:

- periodic interval from config (`IOTDB_POLL_INTERVAL_LEN_IN_SEC`)
- checks datapoint changes in `(previous_timestamp, current_timestamp]`
- sends data messages only when changes are found
- unsubscribe and unsubscribe-client cleanup stop polling when no subscriptions remain

## Configuration

Create `/docker/.env` (if missing) and configure:

```shell
#########################
# GENERAL CONFIGURATION #
#########################

HANDLER_TYPE=iotdb
DATA_POINTS_SCHEMA_FILE=vss_data_points.yaml

#######################
# IOTDB CONFIGURATION #
#######################

IOTDB_HOST="your-iotdb-host"
IOTDB_PORT=6667
IOTDB_USER="your-iotdb-user"
IOTDB_PASSWORD="your-iotdb-password"
IOTDB_TIMEZONE="your-time-zone"
IOTDB_FETCH_SIZE=10000
IOTDB_POLL_INTERVAL_LEN_IN_SEC=5
```

> [!WARNING]
> Do not commit /docker/.env to GitHub.

### Running

The IoTDB handler is started by the Websocket-Server (see project root README).
No manual standalone startup is required for normal project execution.

### Validation

Before merging changes in this area, run:

```shell
npm test
npm run build
```
