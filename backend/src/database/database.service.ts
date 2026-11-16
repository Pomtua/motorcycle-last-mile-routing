import { Injectable, Logger, OnModuleDestroy, OnModuleInit } from '@nestjs/common';
import { readdir, readFile } from 'node:fs/promises';
import { join } from 'node:path';
import { Pool, PoolClient, QueryResultRow } from 'pg';
import { config } from '../config';

export type Queryable = Pick<PoolClient, 'query'>;

@Injectable()
export class DatabaseService implements OnModuleInit, OnModuleDestroy {
  private readonly logger = new Logger(DatabaseService.name);
  readonly pool = new Pool({
    connectionString: config.databaseUrl,
    max: 20,
    options: `-c search_path=${config.databaseSchema},public`,
  });

  async onModuleInit(): Promise<void> {
    await this.migrate();
  }

  async onModuleDestroy(): Promise<void> {
    await this.pool.end();
  }

  async query<T extends QueryResultRow>(text: string, values: unknown[] = []): Promise<T[]> {
    const result = await this.pool.query<T>(text, values);
    return result.rows;
  }

  async one<T extends QueryResultRow>(text: string, values: unknown[] = []): Promise<T | undefined> {
    const rows = await this.query<T>(text, values);
    return rows[0];
  }

  async transaction<T>(work: (client: PoolClient) => Promise<T>): Promise<T> {
    const client = await this.pool.connect();
    try {
      await client.query('BEGIN');
      const result = await work(client);
      await client.query('COMMIT');
      return result;
    } catch (error) {
      await client.query('ROLLBACK');
      throw error;
    } finally {
      client.release();
    }
  }

  private async migrate(): Promise<void> {
    await this.pool.query(`CREATE SCHEMA IF NOT EXISTS ${config.databaseSchema}`);
    await this.pool.query(
      'CREATE TABLE IF NOT EXISTS schema_migrations (name text PRIMARY KEY, applied_at timestamptz NOT NULL DEFAULT now())',
    );
    const applied = new Set(
      (await this.query<{ name: string }>('SELECT name FROM schema_migrations')).map((row) => row.name),
    );
    const files = (await readdir(config.migrationsDir)).filter((name) => name.endsWith('.sql')).sort();
    for (const name of files) {
      if (applied.has(name)) {
        continue;
      }
      const sql = await readFile(join(config.migrationsDir, name), 'utf8');
      await this.transaction(async (client) => {
        await client.query(sql);
        await client.query('INSERT INTO schema_migrations (name) VALUES ($1)', [name]);
      });
      this.logger.log(`applied migration ${name}`);
    }
  }
}
