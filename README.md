# Motorcycle Last-Mile Routing

Routing engine (C++), REST API (NestJS) and web simulation dashboard (Next.js) for motorcycle last-mile delivery with weight, volume, time windows, split delivery and delivery zones. Distances and travel times come from OSRM with a motorcycle profile on OpenStreetMap data.

```
frontend (Next.js :3000) ──HTTP/SSE──▶ backend (NestJS :4000) ──stdin/stdout──▶ engine (C++ solve)
                                            │            │
                                     PostgreSQL+PostGIS  OSRM (:5000)
```

## Requirements

- Docker with Compose v2.
- A prepared OSRM graph in `infra/osrm/graph/thailand-latest.osrm*`, built with `infra/osrm/profiles/motorcycle.lua`.
- `.env` in the project root:
  ```
  POSTGRES_USER=admin
  POSTGRES_PASSWORD=change-me
  POSTGRES_DB=osm_routing_db
  POSTGRES_PORT=5432
  DB_DATA_PATH=./data/postgres_data
  ```
  Optional:
  - `ENGINE_CONCURRENCY`: parallel solves (default CPU cores − 1).
  - `PUBLIC_API_URL`: API address the browser uses (default `http://localhost:4000/api`).

## Run with Docker

```bash
docker compose up -d --build db osrm backend frontend
```

Open http://localhost:3000. API health: http://localhost:4000/api/health.

The first build compiles the OSRM patch, downloads OR-Tools v9.12 and compiles the engine, so it takes a while. The backend creates its tables in the `app` schema on start.

## Run for development

Engine:

```bash
bash engine/scripts/setup_ortools.sh
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release -DWITH_ORTOOLS=ON
cmake --build engine/build -j
```

Database and OSRM:

```bash
docker compose up -d db osrm
```

Backend (uses `engine/build/solve`):

```bash
cd backend && pnpm install && pnpm build && DATABASE_URL=postgres://admin:change-me@localhost:5432/osm_routing_db pnpm start
```

Frontend:

```bash
cd frontend && pnpm install && pnpm dev
```

## Using the dashboard

1. **Upload Parcel CSV** (template: *Download template CSV*), or drop a benchmark instance JSON from `data/instances`. Errors are listed by row.
2. Set **Fleet & Constraints**: number of riders, max weight (kg), max volume (L), shift start and end. Imported instances fill these in.
3. Choose one or more **algorithms**: C++ Engine (HGS), OR-Tools, Baseline (NN), I1+LS.
   - The ⚙ button opens each algorithm's settings. The default C++ budget is 0.1 s per customer, and OR-Tools gets the same time as the C++ Engine.
   - Turn on **Zone Clustering** to keep each rider inside as few zones as possible.
4. **Run Simulation**. Progress is shown live and can be cancelled. NN always runs as the reference.
5. Read the results:
   - **Simulation Map**: routes on real roads, zones, and stop details on click.
   - **Fleet Schedule**: per-rider load, timeline, stop sequence with ETAs, and CSV export.
   - **Analytics**: algorithm comparison, charts and run history.
   - **Benchmark**: the offline 432-instance results and the time-budget sweep.

CSV columns:

| Column | Meaning |
|---|---|
| `type` | `depot` (exactly one row) or `customer` |
| `id` | Parcel id, unique |
| `lat`, `lng` | WGS84 coordinates |
| `weight_kg`, `volume_l` | Parcel weight (≥ 0.01, at most 2 decimals) and volume (≥ 0.1, at most 1 decimal); the engine works in 0.01 kg and 0.1 L steps, like the benchmark instances |
| `tw_start`, `tw_end` | Delivery window, `HH:MM` |
| `service_min` | Minutes spent at the stop |

Every CSV point is moved onto its nearest road. **Max distance to road** (under the upload box, default 200 m) sets how far that may be; a file with a point farther away is rejected and the rows are listed. Imported benchmark instances are already snapped.

Sample CSVs with real Bangkok addresses can be made from any benchmark instance (t = 0 becomes 08:00):

```bash
python3 tools/instance_generator/instance_to_csv.py data/instances/C_D1_n100_loose_s42_instance.json parcels-100.csv
```

`--limit N` keeps the first N parcels. The script prints the instance's own fleet, which is a good starting point for Fleet & Constraints.

Give a directory to convert every `*_instance.json` in it; the output directory also gets `fleets.csv` with each file's riders, capacities and shift:

```bash
python3 tools/instance_generator/instance_to_csv.py data/instances data/instance_csv
```

## Tests

```bash
ctest --test-dir engine/build --output-on-failure
```

```bash
cd backend && pnpm test
```

## Benchmark tooling

The benchmark that the report is based on lives in `tools/benchmark` (`run_full.py`, `summarize_full.py`, `summarize_budgets.py`); summaries are in `results/`.
