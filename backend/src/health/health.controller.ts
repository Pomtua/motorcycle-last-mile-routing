import { Controller, Get } from '@nestjs/common';
import { config } from '../config';
import { DatabaseService } from '../database/database.service';
import { EngineService } from '../engine/engine.service';
import { OsrmService } from '../osrm/osrm.service';

@Controller('health')
export class HealthController {
  constructor(
    private readonly db: DatabaseService,
    private readonly osrm: OsrmService,
    private readonly engine: EngineService,
  ) {}

  @Get()
  async health() {
    const [database, osrm, engine] = await Promise.all([
      this.db.query('SELECT 1').then(() => true, () => false),
      this.osrm.ping(),
      this.engine.available(),
    ]);
    return {
      ok: database && osrm && engine,
      database,
      osrm,
      engine,
      engineConcurrency: config.engineConcurrency,
    };
  }
}
