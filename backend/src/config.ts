import { availableParallelism } from 'node:os';
import { resolve } from 'node:path';

function integer(name: string, fallback: number, minimum: number): number {
  const raw = process.env[name];
  if (raw === undefined || raw === '') {
    return fallback;
  }
  const value = Number(raw);
  if (!Number.isInteger(value) || value < minimum) {
    throw new Error(`${name} must be an integer >= ${minimum}, got ${raw}`);
  }
  return value;
}

function schemaName(value: string): string {
  if (!/^[a-z_][a-z0-9_]*$/.test(value)) {
    throw new Error(`DATABASE_SCHEMA must be a lowercase SQL identifier, got ${value}`);
  }
  return value;
}

export const config = {
  port: integer('PORT', 4000, 1),
  databaseUrl: process.env.DATABASE_URL ?? 'postgres://admin:admin@localhost:5432/osm_routing_db',
  databaseSchema: schemaName(process.env.DATABASE_SCHEMA ?? 'app'),
  osrmUrl: (process.env.OSRM_URL ?? 'http://localhost:5000').replace(/\/+$/, ''),
  osrmProfile: process.env.OSRM_PROFILE ?? 'motorcycle',
  osrmTimeoutMs: integer('OSRM_TIMEOUT_MS', 120_000, 1000),
  enginePath: resolve(process.env.ENGINE_PATH ?? resolve(__dirname, '../../engine/build/solve')),
  engineConcurrency: integer('ENGINE_CONCURRENCY', Math.max(1, availableParallelism() - 1), 1),
  migrationsDir: resolve(process.env.MIGRATIONS_DIR ?? resolve(__dirname, '../migrations')),
  resultsDir: resolve(process.env.RESULTS_DIR ?? resolve(__dirname, '../../results')),
  corsOrigin: process.env.CORS_ORIGIN ?? '*',
  maxUploadBytes: integer('MAX_UPLOAD_BYTES', 200 * 1024 * 1024, 1024),
  maxCustomers: integer('MAX_CUSTOMERS', 1999, 1),
};
