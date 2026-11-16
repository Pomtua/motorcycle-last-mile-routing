CREATE EXTENSION IF NOT EXISTS postgis SCHEMA public;

CREATE TYPE algorithm AS ENUM ('hgs', 'i1_ls', 'nn', 'ortools');
CREATE TYPE job_status AS ENUM ('queued', 'running', 'succeeded', 'failed', 'cancelled');

CREATE TABLE datasets (
  id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  name        text NOT NULL,
  source      text NOT NULL CHECK (source IN ('csv', 'instance')),
  depot       geometry(Point, 4326) NOT NULL,
  defaults    jsonb,
  created_at  timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE customers (
  id             bigserial PRIMARY KEY,
  dataset_id     uuid NOT NULL REFERENCES datasets(id) ON DELETE CASCADE,
  node_index     integer NOT NULL CHECK (node_index >= 1),
  external_id    text NOT NULL,
  location       geometry(Point, 4326) NOT NULL,
  demand_weight  double precision NOT NULL CHECK (demand_weight > 0),
  demand_volume  double precision NOT NULL CHECK (demand_volume > 0),
  tw_start       time NOT NULL,
  tw_end         time NOT NULL,
  service_s      integer NOT NULL DEFAULT 0 CHECK (service_s >= 0),
  UNIQUE (dataset_id, node_index),
  UNIQUE (dataset_id, external_id),
  CHECK (tw_start <= tw_end)
);
CREATE INDEX customers_location_gix ON customers USING gist (location);

CREATE TABLE matrices (
  dataset_id    uuid PRIMARY KEY REFERENCES datasets(id) ON DELETE CASCADE,
  size          integer NOT NULL CHECK (size >= 2),
  durations_s   bytea NOT NULL,
  distances_m   bytea NOT NULL,
  source        text NOT NULL,
  created_at    timestamptz NOT NULL DEFAULT now(),
  CHECK (octet_length(durations_s) = size * size * 8),
  CHECK (octet_length(distances_m) = size * size * 8)
);

CREATE TABLE runs (
  id                  uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  dataset_id          uuid NOT NULL REFERENCES datasets(id) ON DELETE CASCADE,
  seed                bigint NOT NULL DEFAULT 42 CHECK (seed >= 0 AND seed < 4294967296),
  fleet_size          integer NOT NULL CHECK (fleet_size > 0),
  weight_capacity     double precision NOT NULL CHECK (weight_capacity > 0),
  volume_capacity     double precision NOT NULL CHECK (volume_capacity > 0),
  shift_start         time NOT NULL,
  shift_end           time NOT NULL,
  zoning              boolean NOT NULL DEFAULT false,
  num_zones           integer CHECK (num_zones IS NULL OR num_zones >= 1),
  zone_penalty_alpha  double precision NOT NULL DEFAULT 0.25 CHECK (zone_penalty_alpha >= 0),
  created_at          timestamptz NOT NULL DEFAULT now(),
  CHECK (shift_start < shift_end)
);
CREATE INDEX runs_dataset_idx ON runs (dataset_id, created_at DESC);

CREATE TABLE jobs (
  id                uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  run_id            uuid NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
  algorithm         algorithm NOT NULL,
  settings          jsonb NOT NULL,
  status            job_status NOT NULL DEFAULT 'queued',
  error             text,
  started_at        timestamptz,
  finished_at       timestamptz,
  time_limit_s      double precision,
  runtime_ms        double precision,
  total_distance_m  double precision,
  total_duration_s  double precision,
  vehicles_used     integer,
  feasible          boolean,
  capacity_ok       boolean,
  on_time_ratio     double precision,
  metrics           jsonb,
  UNIQUE (run_id, algorithm)
);

CREATE TABLE zones (
  id              bigserial PRIMARY KEY,
  job_id          uuid NOT NULL REFERENCES jobs(id) ON DELETE CASCADE,
  zone_index      integer NOT NULL,
  hull            geometry(Geometry, 4326) NOT NULL,
  customer_count  integer NOT NULL,
  UNIQUE (job_id, zone_index)
);
CREATE INDEX zones_hull_gix ON zones USING gist (hull);

CREATE TABLE routes (
  id             bigserial PRIMARY KEY,
  job_id         uuid NOT NULL REFERENCES jobs(id) ON DELETE CASCADE,
  vehicle_index  integer NOT NULL,
  distance_m     double precision NOT NULL,
  duration_s     double precision NOT NULL,
  load_weight    double precision NOT NULL,
  load_volume    double precision NOT NULL,
  return_s       double precision NOT NULL,
  path           geometry(LineString, 4326),
  UNIQUE (job_id, vehicle_index)
);

CREATE TABLE stops (
  id                bigserial PRIMARY KEY,
  route_id          bigint NOT NULL REFERENCES routes(id) ON DELETE CASCADE,
  sequence          integer NOT NULL CHECK (sequence >= 1),
  customer_id       bigint NOT NULL REFERENCES customers(id) ON DELETE CASCADE,
  delivered_weight  double precision NOT NULL,
  delivered_volume  double precision NOT NULL,
  arrival_s         double precision NOT NULL,
  service_start_s   double precision NOT NULL,
  departure_s       double precision NOT NULL,
  on_time           boolean NOT NULL,
  zone_index        integer,
  UNIQUE (route_id, sequence)
);
CREATE INDEX stops_customer_idx ON stops (customer_id);
