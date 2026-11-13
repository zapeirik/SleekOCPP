# SleekOCPP

An OCPP 1.6 central system (the server chargers connect to) for local development and testing,
written in C++ with Qt 6. It runs either with a Qt Widgets GUI or headless, and offers a REST API.

- Chargers connect with plain `ws://<host>:9000/<charge point id>` (subprotocol `ocpp1.6`). Any charger may connect, there is no authentication.
- Everything is stored in SQLite: chargers, connector status, transactions, meter values, configuration cache and every OCPP message.
- The server runs inside the application process, in both GUI and headless mode.
- Scale: made for 3 to 20 chargers.

## Build and run (macOS)

Needs Qt 6.5 or newer (developed with 6.11) including Qt WebSockets, Qt HTTP Server and the SQLite driver, and CMake.

    qt-cmake -S . -B build
    cmake --build build -j8
    ctest --test-dir build --output-on-failure

    ./build/src/sleekocpp                 # GUI
    ./build/src/sleekocpp --headless      # no GUI, logs to the console

If a port is already in use, the GUI stays open and shows a red banner; headless exits with code 1.

### Options

| Option | Default | Meaning |
| --- | --- | --- |
| `--gui` / `--headless` | `--gui` | Front end |
| `-p`, `--port` | 9000 | OCPP WebSocket port |
| `--rest-port` | 8080 | REST API port |
| `--rest-bind` | 127.0.0.1 | REST API address. Use `0.0.0.0` for all interfaces (no authentication!) |
| `--db` | app data folder | SQLite file |
| `--log-level` | info | debug, info, warning, error |
| `--log-file` | console | Write the log to a file |
| `--id-tag` | SLEEK | idTag pre-filled for remote start |

The same settings can be put in `~/Library/Preferences/SleekOCPP/sleekocpp.ini` (`ocppPort`, `restPort`,
`restBind`, `dbPath`, `logLevel`, `logFile`, `defaultIdTag`). Order: defaults, ini file, command line.

## The GUI

- **List:** all chargers ever seen, with connection state and per connector status (colored).
- **Messages tab:** every OCPP frame of the selected charger, newest first, with the selected frame as formatted JSON. The **Filter** menu hides or shows messages per action (requests and their responses together), e.g. show only StartTransaction and StopTransaction.
  The **Prune** menu cleans up the log in two steps: *Delete messages...* hides messages matching charger, message types, call types (Call/CallResult/CallError) and/or "older than" a date (with confirmation); *Remove deleted from database...* then erases the hidden ones for good, for this charger or all (with confirmation).
- **Configuration tab:** read all keys (GetConfiguration), double-click a value to change it (ChangeConfiguration), set keys that are not listed.
- **Control tab:** start charging (idTag typed per start), stop charging, SetChargingProfile (single-period limit), clear profiles, Reset, TriggerMessage and raw messages.
- **Transactions tab (per charger):** whole transaction history of the selected charger, sortable. The top-level **Transactions** tab next to *Chargers* shows all chargers. Energy is meterStop − meterStart in Wh as reported. Selecting a row shows its details under the grid: a summary and every meter value reported for it (periodic MeterValues and the `transactionData` of StopTransaction, marked in the Source column) with the interval since the previous sample of the same measurand, plus min/average/max interval.

## REST API

JSON over HTTP, no authentication. `<id>` is the charge point id.

| Request | Meaning |
| --- | --- |
| `GET /api/charge_points` | All chargers with connectors |
| `GET /api/charge_points/<id>` | One charger, including the last transaction |
| `GET /api/transactions?limit=N` | Transaction history of all chargers, newest first (no limit = everything) |
| `GET /api/transactions/<id>/meter_values` | The transaction and all its meter values, oldest first, with `source` and `intervalSeconds` (404 if unknown) |
| `GET /api/charge_points/<id>/transactions?limit=N` | Transaction history of one charger (404 if unknown) |
| `GET /api/charge_points/<id>/messages?limit=N` | Logged OCPP frames, newest first (soft-deleted ones excluded) |
| `POST /api/messages/delete` | Step 1, no confirmation: body `{chargePointId?, actions?[], types?[], before?}` (ISO 8601); marks matching messages deleted, returns `{"deleted": n}` |
| `POST /api/messages/purge` | Step 2, no confirmation: body `{chargePointId?}`; removes deleted messages from the database, returns `{"removed": n}` |
| `GET /api/charge_points/<id>/meter_values?limit=N` | Recent meter values |
| `GET /api/charge_points/<id>/configuration` | Cached configuration |
| `POST /api/charge_points/<id>/<command>` | Send a command (JSON body) |

Commands and bodies:

| Command | Body |
| --- | --- |
| `remote_start` | `{"idTag": "TAG", "connectorId": 1, "chargingProfile": {...}}` (connectorId and chargingProfile optional) |
| `remote_stop` | `{"transactionId": 12}` (optional: the running transaction is used) |
| `get_configuration` | `{"keys": ["HeartbeatInterval"]}` (optional: all keys) |
| `change_configuration` | `{"key": "HeartbeatInterval", "value": "30"}` |
| `set_charging_profile` | `{"connectorId": 1, "csChargingProfiles": {...}}` |
| `clear_charging_profile` | `{"id": 1, "connectorId": 1, "purpose": "TxProfile", "stackLevel": 0}` (all optional) |
| `get_composite_schedule` | `{"connectorId": 1, "duration": 3600, "chargingRateUnit": "A"}` |
| `reset` | `{"type": "Soft"}` or `"Hard"` |
| `trigger_message` | `{"requestedMessage": "StatusNotification", "connectorId": 1}` |
| `unlock_connector` | `{"connectorId": 1}` |
| `change_availability` | `{"connectorId": 1, "type": "Operative"}` or `"Inoperative"` |
| `send` | `{"action": "GetLocalListVersion", "payload": {}}` |

The answer is `{"outcome": "result|error|timeout|disconnected", "status": "...", "payload": {...}}`.
HTTP status: 200 answered, 400 bad request, 404 unknown charger, 409 charger not connected, 504 no answer within 30 s.

    curl -X POST localhost:8080/api/charge_points/CP1/remote_start -d '{"idTag":"TAG","connectorId":1}'

## Headless on Linux (Docker)

    docker build -t sleekocpp .
    docker run --rm -p 9000:9000 -p 8080:8080 -v sleekocpp-data:/data sleekocpp

The image builds without the GUI, runs the tests during the build, and keeps the database in the `/data` volume.
The REST API listens on all interfaces inside the container; publish port 8080 only on networks you trust.

## macOS app bundle

    packaging/macos.sh

Builds `build-macos/src/SleekOCPP.app` and `SleekOCPP.dmg` with Qt copied in (`macdeployqt`). It is signed ad hoc,
which is enough on the Mac that built it; giving it to others needs a Developer ID signature and notarization.

## Code layout and how to extend it

    src/main.cpp                    Start-up: config, logging, database, servers, GUI or headless
    src/core/                       No GUI code. Built as the library sleekocpp_core
      AppConfig, Logger             Settings and logging
      ocpp/OcppMessage              Parsing and building [2,id,action,payload] frames
      ocpp/ChargePointSession       One connected charger: sendCall() with timeout, incoming calls
      ocpp/OcppServer               WebSocket server, one session per charger
      CentralSystem                 Handlers for incoming actions, writes to the database
      ChargePointCommands           Commands sent to chargers
      ChargePointModel              Table model for the GUI list
      RestApi                       REST endpoints
      db/Database                   SQLite schema, records and queries
    src/gui/                        Qt Widgets (library sleekocpp_gui)
      MainWindow                    Splitter: list | detail, status bar
      ChargePointListWidget         Charger list
      ChargePointDetailWidget       Header and Messages tab, hosts the other tabs
      ChargePointConfigurationWidget, ChargePointControlWidget, TransactionsWidget
    tests/                          Qt Test, one executable per area (tst_e2e starts the real binary)

How data flows: a frame arrives at `ChargePointSession`, `OcppServer` passes calls to `CentralSystem`,
which writes to the `Database` and emits `chargePointChanged(id)`. The model, the detail view and the REST API read
from the database. Commands go the other way through `ChargePointCommands`, which sends a call and reports a
`CallOutcome` (result, error, timeout or disconnected) to a callback.

**Handle another incoming action:** add `Reply handleXxx(const QString &cpId, const QJsonObject &p)` to `CentralSystem` and register it in the handler
table in the constructor. Return `Reply::ok({...})` or `Reply::error(code, text)`. Add a test in `tests/tst_centralsystem.cpp`.

**Send another command:** add a method to `ChargePointCommands` that builds the payload and calls `send()`.
Then expose it: a case in `RestApi` with `addCommand("name", ...)`, and a button in `ChargePointControlWidget`
(call the command and pass the outcome to `showOutcome()`). The raw message box can send anything meanwhile.

**Store something new:** add a table or column in `Database` as a new step in the migrations (they use
`PRAGMA user_version`, so existing databases are upgraded), a record struct and a query method.

**Add a GUI tab:** write a widget with `setChargePoint(id)` and register it in `MainWindow` with
`detail()->addTab(widget, "Title")` and a connection from `ChargePointListWidget::chargePointSelected`.

**Add a setting:** add a field to `AppConfig`, then `load()`/`save()` and, if it should be a command line option,
`addOptions()`/`applyCommandLine()`.

## Not included

OCPP 2.0.1, TLS (`wss://`) and charger authentication, REST authentication, `/files` and `status.json` from the
Python reference. Nothing is pruned automatically: the database grows until you prune messages (Messages tab or REST).
The GUI has only been tried on macOS (and offscreen in tests).
