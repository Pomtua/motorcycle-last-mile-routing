import { BadRequestException, Injectable, NotFoundException } from '@nestjs/common';
import { PoolClient } from 'pg';
import { config } from '../config';
import { DatabaseService } from '../database/database.service';
import { CustomerRow, decodeMatrix, encodeMatrix } from '../domain/instance-builder';
import { parseInstanceJson } from '../domain/instance-import';
import { ParcelError, ParcelSet, parseParcelCsv } from '../domain/parcels';
import { snapErrors } from '../domain/snapping';
import { formatClock } from '../domain/time';
import { OsrmService } from '../osrm/osrm.service';

export interface DatasetDefaults {
  fleetSize: number;
  weightCapacityKg: number;
  volumeCapacityL: number;
  shiftStart: string;
  shiftEnd: string;
}

interface Snapping {
  maxDistance: number;
  depotInput: { lng: number; lat: number };
  depotDistance: number;
  customerInputs: { lng: number; lat: number }[];
  customerDistances: number[];
}

export interface DatasetMatrix {
  size: number;
  durations: Float64Array;
  distances: Float64Array;
}

@Injectable()
export class DatasetsService {
  constructor(
    private readonly db: DatabaseService,
    private readonly osrm: OsrmService,
  ) {}

  async createFromCsv(name: string, text: string, maxSnapDistance: number) {
    const parcels = this.parse(() => parseParcelCsv(text, config.maxCustomers));
    const points = [parcels.depot, ...parcels.customers].map((p) => [p.lng, p.lat] as [number, number]);
    const labels = ['Depot', ...parcels.customers.map((c) => `Parcel ${c.externalId}`)];
    const table = await this.osrm.table(points, labels);
    const rows = [parcels.rows.depot, ...parcels.rows.customers];
    const errors = snapErrors(
      table.waypoints.map((waypoint, index) => ({ row: rows[index], label: labels[index], distance: waypoint.distance })),
      maxSnapDistance,
    );
    if (errors.length > 0) {
      throw new BadRequestException({ message: 'Some points are too far from a road', errors });
    }
    const [depotPoint, ...customerPoints] = table.waypoints;
    const snapped: ParcelSet = {
      depot: { lng: depotPoint.location[0], lat: depotPoint.location[1] },
      customers: parcels.customers.map((customer, index) => ({
        ...customer,
        lng: customerPoints[index].location[0],
        lat: customerPoints[index].location[1],
      })),
    };
    const snapping: Snapping = {
      maxDistance: maxSnapDistance,
      depotInput: parcels.depot,
      depotDistance: depotPoint.distance,
      customerInputs: parcels.customers.map((customer) => ({ lng: customer.lng, lat: customer.lat })),
      customerDistances: customerPoints.map((point) => point.distance),
    };
    const matrix = { size: table.size, durations: table.durations, distances: table.distances };
    return this.store(name, 'csv', snapped, { ...matrix, source: `osrm:${config.osrmProfile}` }, null, snapping);
  }

  async createFromInstance(name: string, text: string) {
    const imported = this.parse(() => parseInstanceJson(text, config.maxCustomers));
    const defaults: DatasetDefaults = {
      fleetSize: imported.defaults.fleetSize,
      weightCapacityKg: imported.defaults.weightCapacityKg,
      volumeCapacityL: imported.defaults.volumeCapacityL,
      shiftStart: formatClock(imported.defaults.shiftStartS).slice(0, 5),
      shiftEnd: formatClock(imported.defaults.shiftEndS).slice(0, 5),
    };
    return this.store(
      name,
      'instance',
      imported.parcels,
      { size: imported.size, durations: imported.durations, distances: imported.distances, source: 'instance' },
      defaults,
      null,
    );
  }

  async list() {
    return this.db.query(
      `SELECT d.id, d.name, d.source, d.defaults, d.created_at,
              (SELECT count(*)::int FROM customers c WHERE c.dataset_id = d.id) AS customer_count,
              (SELECT count(*)::int FROM runs r WHERE r.dataset_id = d.id) AS run_count
         FROM datasets d
        ORDER BY d.created_at DESC`,
    );
  }

  async detail(id: string) {
    const dataset = await this.db.one<{
      id: string;
      name: string;
      source: string;
      defaults: DatasetDefaults | null;
      created_at: string;
      depot: { lng: number; lat: number };
      depot_snap_distance_m: number | null;
      max_snap_distance_m: number | null;
    }>(
      `SELECT id, name, source, defaults, created_at,
              json_build_object('lng', ST_X(depot), 'lat', ST_Y(depot)) AS depot,
              depot_snap_m AS depot_snap_distance_m, max_snap_m AS max_snap_distance_m
         FROM datasets WHERE id = $1`,
      [id],
    );
    if (!dataset) {
      throw new NotFoundException(`dataset ${id} not found`);
    }
    const customers = await this.customers(id);
    const snaps = await this.db.query<{ node_index: number; snap_m: number | null }>(
      'SELECT node_index, snap_m FROM customers WHERE dataset_id = $1 ORDER BY node_index',
      [id],
    );
    const snapOf = new Map(snaps.map((row) => [row.node_index, row.snap_m]));
    const totals = customers.reduce(
      (sum, c) => ({ weight: sum.weight + c.demand_weight, volume: sum.volume + c.demand_volume }),
      { weight: 0, volume: 0 },
    );
    return {
      ...dataset,
      customer_count: customers.length,
      total_weight_kg: totals.weight,
      total_volume_l: totals.volume * 1000,
      customers: customers.map((c) => ({
        id: c.id,
        node_index: c.node_index,
        external_id: c.external_id,
        lng: c.lng,
        lat: c.lat,
        weight_kg: c.demand_weight,
        volume_l: c.demand_volume * 1000,
        tw_start: formatClock(c.tw_start_s).slice(0, 5),
        tw_end: formatClock(c.tw_end_s).slice(0, 5),
        service_min: c.service_s / 60,
        snap_distance_m: snapOf.get(c.node_index) ?? null,
      })),
    };
  }

  async remove(id: string): Promise<void> {
    const rows = await this.db.query('DELETE FROM datasets WHERE id = $1 RETURNING id', [id]);
    if (rows.length === 0) {
      throw new NotFoundException(`dataset ${id} not found`);
    }
  }

  async depot(id: string): Promise<{ lng: number; lat: number }> {
    const row = await this.db.one<{ lng: number; lat: number }>(
      'SELECT ST_X(depot) AS lng, ST_Y(depot) AS lat FROM datasets WHERE id = $1',
      [id],
    );
    if (!row) {
      throw new NotFoundException(`dataset ${id} not found`);
    }
    return row;
  }

  async customers(id: string): Promise<CustomerRow[]> {
    return this.db.query<CustomerRow>(
      `SELECT id::text, node_index, external_id, ST_X(location) AS lng, ST_Y(location) AS lat,
              demand_weight, demand_volume,
              EXTRACT(EPOCH FROM tw_start)::int AS tw_start_s,
              EXTRACT(EPOCH FROM tw_end)::int AS tw_end_s,
              service_s
         FROM customers WHERE dataset_id = $1 ORDER BY node_index`,
      [id],
    );
  }

  async matrix(id: string): Promise<DatasetMatrix> {
    const row = await this.db.one<{ size: number; durations_s: Buffer; distances_m: Buffer }>(
      'SELECT size, durations_s, distances_m FROM matrices WHERE dataset_id = $1',
      [id],
    );
    if (!row) {
      throw new NotFoundException(`dataset ${id} has no distance matrix`);
    }
    return { size: row.size, durations: decodeMatrix(row.durations_s), distances: decodeMatrix(row.distances_m) };
  }

  private parse<T>(work: () => T): T {
    try {
      return work();
    } catch (error) {
      if (error instanceof ParcelError) {
        throw new BadRequestException({ message: 'The file has errors', errors: error.errors });
      }
      throw error;
    }
  }

  private async store(
    name: string,
    source: 'csv' | 'instance',
    parcels: ParcelSet,
    matrix: DatasetMatrix & { source: string },
    defaults: DatasetDefaults | null,
    snapping: Snapping | null,
  ) {
    const trimmed = name.trim();
    if (!trimmed) {
      throw new BadRequestException('name is required');
    }
    const id = await this.db.transaction(async (client: PoolClient) => {
      const inserted = await client.query<{ id: string }>(
        `INSERT INTO datasets (name, source, depot, defaults, depot_input, depot_snap_m, max_snap_m)
         VALUES ($1, $2, ST_SetSRID(ST_MakePoint($3, $4), 4326), $5,
                 CASE WHEN $6::float8 IS NULL THEN NULL ELSE ST_SetSRID(ST_MakePoint($6, $7), 4326) END, $8, $9)
         RETURNING id`,
        [
          trimmed,
          source,
          parcels.depot.lng,
          parcels.depot.lat,
          defaults,
          snapping?.depotInput.lng ?? null,
          snapping?.depotInput.lat ?? null,
          snapping?.depotDistance ?? null,
          snapping?.maxDistance ?? null,
        ],
      );
      const datasetId = inserted.rows[0].id;
      const c = parcels.customers;
      await client.query(
        `INSERT INTO customers (dataset_id, node_index, external_id, location, demand_weight, demand_volume,
                                tw_start, tw_end, service_s, input_location, snap_m)
         SELECT $1, t.ord, t.external_id, ST_SetSRID(ST_MakePoint(t.lng, t.lat), 4326), t.weight, t.volume,
                make_interval(secs => t.tw_start)::time, make_interval(secs => t.tw_end)::time, t.service,
                CASE WHEN t.input_lng IS NULL THEN NULL ELSE ST_SetSRID(ST_MakePoint(t.input_lng, t.input_lat), 4326) END,
                t.snap_m
           FROM unnest($2::text[], $3::float8[], $4::float8[], $5::float8[], $6::float8[], $7::int[], $8::int[], $9::int[],
                       $10::float8[], $11::float8[], $12::float8[])
                WITH ORDINALITY AS t(external_id, lng, lat, weight, volume, tw_start, tw_end, service,
                                     input_lng, input_lat, snap_m, ord)`,
        [
          datasetId,
          c.map((x) => x.externalId),
          c.map((x) => x.lng),
          c.map((x) => x.lat),
          c.map((x) => x.demandWeight),
          c.map((x) => x.demandVolume),
          c.map((x) => Math.round(x.twStartS)),
          c.map((x) => Math.round(x.twEndS)),
          c.map((x) => Math.round(x.serviceS)),
          c.map((_, i) => snapping?.customerInputs[i].lng ?? null),
          c.map((_, i) => snapping?.customerInputs[i].lat ?? null),
          c.map((_, i) => snapping?.customerDistances[i] ?? null),
        ],
      );
      await client.query(
        'INSERT INTO matrices (dataset_id, size, durations_s, distances_m, source) VALUES ($1, $2, $3, $4, $5)',
        [datasetId, matrix.size, encodeMatrix(matrix.durations), encodeMatrix(matrix.distances), matrix.source],
      );
      return datasetId;
    });
    return this.detail(id);
  }
}
