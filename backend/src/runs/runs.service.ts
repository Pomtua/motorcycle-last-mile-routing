import { BadRequestException, Injectable, NotFoundException } from '@nestjs/common';
import { DatabaseService } from '../database/database.service';
import { DatasetsService } from '../datasets/datasets.service';
import { unservableCustomers } from '../domain/instance-builder';
import { parseRunRequest, RunRequestError } from '../domain/run-request';
import { formatClock } from '../domain/time';
import { JobRunner } from './job-runner.service';

const JOB_COLUMNS = `j.id, j.algorithm, j.settings, j.status, j.error, j.started_at, j.finished_at, j.time_limit_s,
  j.runtime_ms, j.total_distance_m, j.total_duration_s, j.vehicles_used, j.feasible, j.capacity_ok,
  j.on_time_ratio, j.metrics`;

const RUN_COLUMNS = `r.id, r.dataset_id, r.seed::float8 AS seed, r.fleet_size, r.weight_capacity,
  r.volume_capacity * 1000 AS volume_capacity_l, to_char(r.shift_start, 'HH24:MI') AS shift_start,
  to_char(r.shift_end, 'HH24:MI') AS shift_end, r.zoning, r.num_zones, r.zone_penalty_alpha, r.created_at`;

@Injectable()
export class RunsService {
  constructor(
    private readonly db: DatabaseService,
    private readonly datasets: DatasetsService,
    private readonly runner: JobRunner,
  ) {}

  async create(body: unknown) {
    let request;
    try {
      request = parseRunRequest(body);
    } catch (error) {
      if (error instanceof RunRequestError) {
        throw new BadRequestException(error.message);
      }
      throw error;
    }
    const customers = await this.datasets.customers(request.datasetId);
    if (customers.length === 0) {
      throw new NotFoundException(`dataset ${request.datasetId} not found`);
    }
    const matrix = await this.datasets.matrix(request.datasetId);
    const unservable = unservableCustomers(customers, matrix, request.shift.startS, request.shift.endS);
    if (unservable.length > 0) {
      const hhmm = (seconds: number) => formatClock(seconds).slice(0, 5);
      const sample = unservable
        .slice(0, 5)
        .map(
          (c) =>
            `${c.externalId} (window ${hhmm(c.window[0])}–${hhmm(c.window[1])}, ` +
            (c.reason === 'late' ? `earliest arrival ${hhmm(c.time)})` : `back at the depot ${hhmm(c.time)} at the earliest)`),
        )
        .join(', ');
      throw new BadRequestException(
        `${unservable.length} parcel(s) cannot be delivered within the shift ${hhmm(request.shift.startS)}–` +
          `${hhmm(request.shift.endS)} even by a rider going there directly: ${sample}${unservable.length > 5 ? ', …' : ''}`,
      );
    }

    const { runId, jobs } = await this.db.transaction(async (client) => {
      const run = await client.query<{ id: string }>(
        `INSERT INTO runs (dataset_id, seed, fleet_size, weight_capacity, volume_capacity, shift_start, shift_end,
                           zoning, num_zones, zone_penalty_alpha)
         VALUES ($1, $2, $3, $4, $5, make_interval(secs => $6)::time, make_interval(secs => $7)::time, $8, $9, $10)
         RETURNING id`,
        [
          request.datasetId,
          request.seed,
          request.fleet.size,
          request.fleet.weightCapacityKg,
          request.fleet.volumeCapacityL / 1000,
          request.shift.startS,
          request.shift.endS,
          request.zoning.enabled,
          request.zoning.enabled ? request.zoning.numZones : null,
          request.zoning.penaltyAlpha,
        ],
      );
      const id = run.rows[0].id;
      const inserted = [];
      for (const settings of request.jobs) {
        const job = await client.query<{ id: string }>(
          'INSERT INTO jobs (run_id, algorithm, settings) VALUES ($1, $2, $3) RETURNING id',
          [id, settings.algorithm, settings],
        );
        inserted.push({ id: job.rows[0].id, runId: id, algorithm: settings.algorithm, settings });
      }
      return { runId: id, jobs: inserted };
    });
    this.runner.enqueue(jobs);
    return { runId, jobIds: jobs.map((job) => job.id) };
  }

  async list(datasetId: string) {
    const runs = await this.db.query<{ id: string }>(
      `SELECT ${RUN_COLUMNS} FROM runs r WHERE r.dataset_id = $1 ORDER BY r.created_at DESC`,
      [datasetId],
    );
    const jobs = await this.db.query<{ run_id: string }>(
      `SELECT j.run_id, ${JOB_COLUMNS} FROM jobs j JOIN runs r ON r.id = j.run_id WHERE r.dataset_id = $1`,
      [datasetId],
    );
    return runs.map((run) => ({ ...run, jobs: jobs.filter((job) => job.run_id === run.id) }));
  }

  async detail(id: string) {
    const run = await this.db.one(`SELECT ${RUN_COLUMNS} FROM runs r WHERE r.id = $1`, [id]);
    if (!run) {
      throw new NotFoundException(`run ${id} not found`);
    }
    const jobs = await this.db.query(`SELECT ${JOB_COLUMNS} FROM jobs j WHERE j.run_id = $1 ORDER BY j.algorithm`, [id]);
    return { ...run, jobs };
  }

  async cancel(id: string) {
    await this.detail(id);
    await this.runner.cancelRun(id);
    return this.detail(id);
  }
}
