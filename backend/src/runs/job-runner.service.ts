import { Injectable, Logger, OnApplicationBootstrap, OnModuleDestroy } from '@nestjs/common';
import { PoolClient } from 'pg';
import { Subject } from 'rxjs';
import { config } from '../config';
import { DatabaseService } from '../database/database.service';
import { DatasetsService } from '../datasets/datasets.service';
import { buildInstance, CustomerRow } from '../domain/instance-builder';
import { JobSettings } from '../domain/run-request';
import { EngineCancelled, EngineService } from '../engine/engine.service';
import { Algorithm, EngineRequest, EngineResult, InstanceJson } from '../engine/engine.types';
import { LngLat, OsrmService } from '../osrm/osrm.service';

export interface JobEvent {
  runId: string;
  jobId: string;
  status: string;
}

interface QueuedJob {
  id: string;
  runId: string;
  algorithm: Algorithm;
  settings: JobSettings;
}

interface PreparedRun {
  instance: InstanceJson;
  customers: CustomerRow[];
  depot: { lng: number; lat: number };
  zoning: EngineRequest['zoning'];
  seed: number;
}

const GEOMETRY_CONCURRENCY = 8;

@Injectable()
export class JobRunner implements OnApplicationBootstrap, OnModuleDestroy {
  private readonly logger = new Logger(JobRunner.name);
  readonly events = new Subject<JobEvent>();
  private readonly pending: QueuedJob[] = [];
  private readonly running = new Map<string, { job: QueuedJob; controller: AbortController }>();
  private readonly prepared = new Map<string, Promise<PreparedRun>>();

  constructor(
    private readonly db: DatabaseService,
    private readonly datasets: DatasetsService,
    private readonly engine: EngineService,
    private readonly osrm: OsrmService,
  ) {}

  async onApplicationBootstrap(): Promise<void> {
    await this.db.query(
      `UPDATE jobs SET status = 'failed', error = 'interrupted by a server restart', finished_at = now()
        WHERE status = 'running'`,
    );
    const queued = await this.db.query<QueuedJob & { run_id: string }>(
      `SELECT j.id, j.run_id, j.algorithm, j.settings
         FROM jobs j JOIN runs r ON r.id = j.run_id
        WHERE j.status = 'queued' ORDER BY r.created_at, j.algorithm`,
    );
    this.enqueue(queued.map((row) => ({ id: row.id, runId: row.run_id, algorithm: row.algorithm, settings: row.settings })));
  }

  onModuleDestroy(): void {
    for (const { controller } of this.running.values()) {
      controller.abort();
    }
  }

  enqueue(jobs: QueuedJob[]): void {
    this.pending.push(...jobs);
    this.pump();
  }

  async cancelRun(runId: string): Promise<void> {
    const removed = this.pending.filter((job) => job.runId === runId);
    this.pending.splice(0, this.pending.length, ...this.pending.filter((job) => job.runId !== runId));
    for (const job of removed) {
      await this.finish(job, 'cancelled', null);
    }
    for (const { job, controller } of this.running.values()) {
      if (job.runId === runId) {
        controller.abort();
      }
    }
  }

  private pump(): void {
    while (this.running.size < config.engineConcurrency) {
      const index = this.pending.findIndex((job) => this.ready(job));
      if (index < 0) {
        return;
      }
      const [job] = this.pending.splice(index, 1);
      const controller = new AbortController();
      this.running.set(job.id, { job, controller });
      void this.execute(job, controller.signal).finally(() => {
        this.running.delete(job.id);
        if (![...this.running.values()].some((entry) => entry.job.runId === job.runId) &&
            !this.pending.some((entry) => entry.runId === job.runId)) {
          this.prepared.delete(job.runId);
        }
        this.pump();
      });
    }
  }

  private ready(job: QueuedJob): boolean {
    if (job.settings.algorithm !== 'ortools' || job.settings.timeLimit !== 'match_hgs') {
      return true;
    }
    const waitsOnHgs = (entry: QueuedJob) => entry.runId === job.runId && entry.algorithm === 'hgs';
    return !this.pending.some(waitsOnHgs) && ![...this.running.values()].some((entry) => waitsOnHgs(entry.job));
  }

  private async execute(job: QueuedJob, signal: AbortSignal): Promise<void> {
    try {
      await this.db.query(`UPDATE jobs SET status = 'running', started_at = now() WHERE id = $1`, [job.id]);
      this.emit(job, 'running');
      const prepared = await this.prepare(job.runId);
      const timeLimit = await this.timeLimit(job, prepared.instance.meta.n);
      if (timeLimit !== null) {
        await this.db.query('UPDATE jobs SET time_limit_s = $2 WHERE id = $1', [job.id, timeLimit]);
      }
      const request: EngineRequest = {
        algorithm: job.algorithm,
        seed: prepared.seed,
        zoning: prepared.zoning,
        instance: prepared.instance,
        ...(timeLimit !== null ? { time_limit_seconds: timeLimit } : {}),
        ...(job.settings.algorithm === 'hgs' ? { split_mode: job.settings.splitMode } : {}),
      };
      const result = await this.engine.solve(request, signal);
      const routeIds = await this.db.transaction((client) => this.store(client, job, prepared, result));
      const geometryError = await this.attachGeometry(routeIds, result, prepared, signal);
      if (signal.aborted) {
        throw new EngineCancelled();
      }
      if (geometryError) {
        await this.db.query(
          `UPDATE jobs SET metrics = metrics || jsonb_build_object('geometry_error', $2::text) WHERE id = $1`,
          [job.id, geometryError],
        );
      }
      await this.finish(job, 'succeeded', null);
    } catch (error) {
      if (error instanceof EngineCancelled || signal.aborted) {
        await this.finish(job, 'cancelled', null);
      } else {
        this.logger.warn(`job ${job.id} (${job.algorithm}) failed: ${(error as Error).message}`);
        await this.finish(job, 'failed', (error as Error).message);
      }
    }
  }

  private async finish(job: QueuedJob, status: 'succeeded' | 'failed' | 'cancelled', error: string | null) {
    await this.db.query('UPDATE jobs SET status = $2, error = $3, finished_at = now() WHERE id = $1', [
      job.id,
      status,
      error,
    ]);
    this.emit(job, status);
  }

  private emit(job: QueuedJob, status: string): void {
    this.events.next({ runId: job.runId, jobId: job.id, status });
  }

  private prepare(runId: string): Promise<PreparedRun> {
    let prepared = this.prepared.get(runId);
    if (!prepared) {
      prepared = this.load(runId);
      prepared.catch(() => this.prepared.delete(runId));
      this.prepared.set(runId, prepared);
    }
    return prepared;
  }

  private async load(runId: string): Promise<PreparedRun> {
    const run = await this.db.one<{
      dataset_id: string;
      seed: string;
      fleet_size: number;
      weight_capacity: number;
      volume_capacity: number;
      shift_start_s: number;
      shift_end_s: number;
      zoning: boolean;
      num_zones: number | null;
      zone_penalty_alpha: number;
    }>(
      `SELECT dataset_id, seed, fleet_size, weight_capacity, volume_capacity, zoning, num_zones, zone_penalty_alpha,
              EXTRACT(EPOCH FROM shift_start)::int AS shift_start_s, EXTRACT(EPOCH FROM shift_end)::int AS shift_end_s
         FROM runs WHERE id = $1`,
      [runId],
    );
    if (!run) {
      throw new Error(`run ${runId} no longer exists`);
    }
    const [depot, customers, matrix] = await Promise.all([
      this.datasets.depot(run.dataset_id),
      this.datasets.customers(run.dataset_id),
      this.datasets.matrix(run.dataset_id),
    ]);
    const seed = Number(run.seed);
    const instance = buildInstance(depot, customers, matrix, {
      seed,
      fleetSize: run.fleet_size,
      weightCapacityKg: run.weight_capacity,
      volumeCapacityM3: run.volume_capacity,
      shiftStartS: run.shift_start_s,
      shiftEndS: run.shift_end_s,
    });
    return {
      instance,
      customers,
      depot,
      seed,
      zoning: {
        enabled: run.zoning,
        num_zones: run.num_zones ?? 'auto',
        penalty_alpha: run.zone_penalty_alpha,
      },
    };
  }

  private async timeLimit(job: QueuedJob, customers: number): Promise<number | null> {
    const settings = job.settings;
    if (settings.algorithm === 'hgs') {
      return settings.timeBudgetPerCustomer * customers;
    }
    if (settings.algorithm !== 'ortools') {
      return null;
    }
    if (settings.timeLimit !== 'match_hgs') {
      return settings.timeLimit;
    }
    const hgs = await this.db.one<{ status: string; runtime_ms: number | null; settings: JobSettings }>(
      `SELECT status, runtime_ms, settings FROM jobs WHERE run_id = $1 AND algorithm = 'hgs'`,
      [job.runId],
    );
    if (hgs?.status === 'succeeded' && hgs.runtime_ms !== null && hgs.runtime_ms > 0) {
      return hgs.runtime_ms / 1000;
    }
    const budget = hgs?.settings.algorithm === 'hgs' ? hgs.settings.timeBudgetPerCustomer : 0.1;
    return budget * customers;
  }

  private async store(client: PoolClient, job: QueuedJob, prepared: PreparedRun, result: EngineResult): Promise<string[]> {
    const customerOf = (node: number) => {
      const customer = prepared.customers[node - 1];
      if (!customer || customer.node_index !== node) {
        throw new Error(`engine returned unknown node ${node}`);
      }
      return customer.id;
    };
    const routes = result.routes;
    const inserted = await client.query<{ id: string; vehicle_index: number }>(
      `INSERT INTO routes (job_id, vehicle_index, distance_m, duration_s, load_weight, load_volume)
       SELECT $1, t.vehicle, t.distance, t.duration, t.weight, t.volume
         FROM unnest($2::int[], $3::float8[], $4::float8[], $5::float8[], $6::float8[])
              AS t(vehicle, distance, duration, weight, volume)
       RETURNING id::text, vehicle_index`,
      [
        job.id,
        routes.map((route) => route.vehicle),
        routes.map((route) => route.distance),
        routes.map((route) => route.duration),
        routes.map((route) => route.load_weight),
        routes.map((route) => route.load_volume),
      ],
    );
    const routeIdByVehicle = new Map(inserted.rows.map((row) => [row.vehicle_index, row.id]));
    const stops = routes.flatMap((route) =>
      route.stops.map((stop, index) => ({ routeId: routeIdByVehicle.get(route.vehicle) as string, sequence: index + 1, stop })),
    );
    await client.query(
      `INSERT INTO stops (route_id, sequence, customer_id, delivered_weight, delivered_volume,
                          arrival_s, service_start_s, departure_s, on_time, zone_index)
       SELECT * FROM unnest($1::bigint[], $2::int[], $3::bigint[], $4::float8[], $5::float8[],
                            $6::float8[], $7::float8[], $8::float8[], $9::bool[], $10::int[])`,
      [
        stops.map((s) => s.routeId),
        stops.map((s) => s.sequence),
        stops.map((s) => customerOf(s.stop.node)),
        stops.map((s) => s.stop.weight),
        stops.map((s) => s.stop.volume),
        stops.map((s) => s.stop.arrival),
        stops.map((s) => s.stop.start),
        stops.map((s) => s.stop.departure),
        stops.map((s) => s.stop.on_time),
        stops.map((s) => s.stop.zone),
      ],
    );
    if (result.zones) {
      const zoneOf = result.zones.zone_of;
      await client.query(
        `INSERT INTO zones (job_id, zone_index, hull, customer_count)
         SELECT $1, z.zone,
                ST_Buffer(ST_ConvexHull(ST_Collect(c.location))::geography, 120)::geometry,
                count(*)::int
           FROM unnest($2::int[], $3::int[]) AS z(node_index, zone)
           JOIN customers c ON c.dataset_id = (SELECT dataset_id FROM runs WHERE id = $4) AND c.node_index = z.node_index
          GROUP BY z.zone`,
        [job.id, zoneOf.slice(1).map((_, index) => index + 1), zoneOf.slice(1), job.runId],
      );
    }
    const validation = result.validation;
    await client.query(
      `UPDATE jobs SET runtime_ms = $2, total_distance_m = $3, total_duration_s = $4, vehicles_used = $5,
                       feasible = $6, capacity_ok = $7, on_time_ratio = $8, metrics = $9
        WHERE id = $1`,
      [
        job.id,
        result.metrics.runtime_ms,
        result.metrics.total_distance,
        result.metrics.total_duration,
        result.metrics.vehicles_used,
        validation.feasible,
        validation.capacity_ok,
        validation.total_stops > 0 ? validation.on_time_stops / validation.total_stops : 1,
        {
          ...result.metrics,
          time_window_ok: validation.time_window_ok,
          violations: validation.violations,
          zones: result.zones ? { ...result.zones, zone_of: undefined } : null,
        },
      ],
    );
    return routes.map((route) => routeIdByVehicle.get(route.vehicle) as string);
  }

  private async attachGeometry(
    routeIds: string[],
    result: EngineResult,
    prepared: PreparedRun,
    signal: AbortSignal,
  ): Promise<string | null> {
    const depot: LngLat = [prepared.depot.lng, prepared.depot.lat];
    const tasks = result.routes.map((route, index) => async () => {
      const points: LngLat[] = [
        depot,
        ...route.stops.map((stop) => {
          const customer = prepared.customers[stop.node - 1];
          return [customer.lng, customer.lat] as LngLat;
        }),
        depot,
      ];
      const path = await this.osrm.route(points);
      await this.db.query(
        `UPDATE routes SET path = ST_SetSRID(ST_GeomFromGeoJSON($2), 4326) WHERE id = $1`,
        [routeIds[index], JSON.stringify({ type: 'LineString', coordinates: path })],
      );
    });
    let firstError: string | null = null;
    let next = 0;
    const worker = async () => {
      while (next < tasks.length && !signal.aborted) {
        const task = tasks[next];
        next += 1;
        try {
          await task();
        } catch (error) {
          firstError ??= (error as Error).message;
        }
      }
    };
    await Promise.all(Array.from({ length: Math.min(GEOMETRY_CONCURRENCY, tasks.length) }, worker));
    return firstError;
  }
}
