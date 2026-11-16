import { Module } from '@nestjs/common';
import { BenchmarkController } from './benchmark/benchmark.controller';
import { DatabaseService } from './database/database.service';
import { DatasetsController } from './datasets/datasets.controller';
import { DatasetsService } from './datasets/datasets.service';
import { EngineService } from './engine/engine.service';
import { HealthController } from './health/health.controller';
import { JobsController } from './jobs/jobs.controller';
import { JobsService } from './jobs/jobs.service';
import { OsrmService } from './osrm/osrm.service';
import { JobRunner } from './runs/job-runner.service';
import { RunsController } from './runs/runs.controller';
import { RunsService } from './runs/runs.service';

@Module({
  controllers: [HealthController, DatasetsController, RunsController, JobsController, BenchmarkController],
  providers: [DatabaseService, OsrmService, EngineService, DatasetsService, JobRunner, RunsService, JobsService],
})
export class AppModule {}
