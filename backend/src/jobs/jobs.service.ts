import { Injectable, NotFoundException } from '@nestjs/common';
import { DatabaseService } from '../database/database.service';
import { formatClock } from '../domain/time';

interface StopRow {
  route_id: string;
  sequence: number;
  customer_id: string;
  external_id: string;
  lng: number;
  lat: number;
  tw_start: string;
  tw_end: string;
  delivered_weight: number;
  delivered_volume: number;
  arrival_s: number;
  service_start_s: number;
  departure_s: number;
  on_time: boolean;
  zone_index: number | null;
  split_count: number;
}

@Injectable()
export class JobsService {
  constructor(private readonly db: DatabaseService) {}

  async result(id: string) {
    const job = await this.db.one<{ run_id: string; dataset_id: string; status: string }>(
      `SELECT j.id, j.run_id, r.dataset_id, j.algorithm, j.settings, j.status, j.error, j.time_limit_s, j.runtime_ms,
              j.total_distance_m, j.total_duration_s, j.vehicles_used, j.feasible, j.capacity_ok, j.on_time_ratio,
              j.metrics, to_char(r.shift_start, 'HH24:MI') AS shift_start, to_char(r.shift_end, 'HH24:MI') AS shift_end,
              EXTRACT(EPOCH FROM r.shift_start)::int AS shift_start_s, r.fleet_size, r.weight_capacity,
              r.volume_capacity * 1000 AS volume_capacity_l,
              json_build_object('lng', ST_X(d.depot), 'lat', ST_Y(d.depot)) AS depot
         FROM jobs j JOIN runs r ON r.id = j.run_id JOIN datasets d ON d.id = r.dataset_id
        WHERE j.id = $1`,
      [id],
    );
    if (!job) {
      throw new NotFoundException(`job ${id} not found`);
    }
    const routes = await this.db.query<{ id: string }>(
      `SELECT id::text, vehicle_index, distance_m, duration_s, load_weight, load_volume * 1000 AS load_volume_l,
              ST_AsGeoJSON(path)::json AS path
         FROM routes WHERE job_id = $1 ORDER BY vehicle_index`,
      [id],
    );
    const stops = await this.stops(id);
    const zones = await this.db.query(
      `SELECT zone_index, customer_count, ST_AsGeoJSON(hull, 6)::json AS hull
         FROM zones WHERE job_id = $1 ORDER BY zone_index`,
      [id],
    );
    return {
      job,
      routes: routes.map(({ id: routeId, ...route }) => ({
        ...route,
        stops: stops
          .filter((stop) => stop.route_id === routeId)
          .map(({ route_id, ...stop }) => ({ ...stop, delivered_volume_l: stop.delivered_volume * 1000 })),
      })),
      zones,
    };
  }

  async exportCsv(id: string): Promise<{ filename: string; body: string }> {
    const job = await this.db.one<{ algorithm: string; shift_start_s: number }>(
      `SELECT j.algorithm, EXTRACT(EPOCH FROM r.shift_start)::int AS shift_start_s
         FROM jobs j JOIN runs r ON r.id = j.run_id WHERE j.id = $1`,
      [id],
    );
    if (!job) {
      throw new NotFoundException(`job ${id} not found`);
    }
    const rows = await this.db.query<StopRow & { vehicle_index: number }>(
      `SELECT ro.vehicle_index, s.sequence, c.external_id, ST_Y(c.location) AS lat, ST_X(c.location) AS lng,
              to_char(c.tw_start, 'HH24:MI') AS tw_start, to_char(c.tw_end, 'HH24:MI') AS tw_end,
              s.delivered_weight, s.delivered_volume, s.arrival_s, s.service_start_s, s.departure_s, s.on_time, s.zone_index
         FROM stops s JOIN routes ro ON ro.id = s.route_id JOIN customers c ON c.id = s.customer_id
        WHERE ro.job_id = $1 ORDER BY ro.vehicle_index, s.sequence`,
      [id],
    );
    const clock = (seconds: number) => formatClock(job.shift_start_s + seconds);
    const escape = (value: string) => (/[",\n]/.test(value) ? `"${value.replace(/"/g, '""')}"` : value);
    const header = 'rider,sequence,parcel_id,lat,lng,delivered_kg,delivered_l,tw_start,tw_end,arrival,service_start,departure,on_time,zone';
    const lines = rows.map((row) =>
      [
        row.vehicle_index + 1,
        row.sequence,
        escape(row.external_id),
        row.lat.toFixed(6),
        row.lng.toFixed(6),
        row.delivered_weight.toFixed(3),
        (row.delivered_volume * 1000).toFixed(3),
        row.tw_start,
        row.tw_end,
        clock(row.arrival_s),
        clock(row.service_start_s),
        clock(row.departure_s),
        row.on_time ? 'yes' : 'no',
        row.zone_index === null ? '' : row.zone_index + 1,
      ].join(','),
    );
    return { filename: `routes-${job.algorithm}-${id.slice(0, 8)}.csv`, body: [header, ...lines].join('\n') + '\n' };
  }

  private stops(jobId: string): Promise<StopRow[]> {
    return this.db.query<StopRow>(
      `SELECT s.route_id::text, s.sequence, c.id::text AS customer_id, c.external_id,
              ST_X(c.location) AS lng, ST_Y(c.location) AS lat,
              to_char(c.tw_start, 'HH24:MI') AS tw_start, to_char(c.tw_end, 'HH24:MI') AS tw_end,
              s.delivered_weight, s.delivered_volume, s.arrival_s, s.service_start_s, s.departure_s, s.on_time,
              s.zone_index,
              (count(*) OVER (PARTITION BY s.customer_id))::int AS split_count
         FROM stops s JOIN routes ro ON ro.id = s.route_id JOIN customers c ON c.id = s.customer_id
        WHERE ro.job_id = $1
        ORDER BY ro.vehicle_index, s.sequence`,
      [jobId],
    );
  }
}
