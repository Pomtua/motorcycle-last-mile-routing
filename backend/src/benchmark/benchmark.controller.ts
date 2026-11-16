import { Controller, Get } from '@nestjs/common';
import { parse } from 'csv-parse/sync';
import { readFile } from 'node:fs/promises';
import { join } from 'node:path';
import { config } from '../config';

async function optional(path: string): Promise<string | null> {
  try {
    return await readFile(path, 'utf8');
  } catch {
    return null;
  }
}

function numeric(record: Record<string, string>): Record<string, number | string | null> {
  return Object.fromEntries(
    Object.entries(record).map(([key, value]) => {
      if (value === '') {
        return [key, null];
      }
      const number = Number(value);
      return [key, Number.isFinite(number) ? number : value];
    }),
  );
}

@Controller('benchmark')
export class BenchmarkController {
  @Get()
  async summary() {
    const finalText = await optional(join(config.resultsDir, 'final', 'summary.json'));
    const budgetsText = await optional(join(config.resultsDir, 'budgets', 'budgets.csv'));
    const final = finalText ? (JSON.parse(finalText) as { solvers: unknown[]; pairs: unknown[] }) : null;
    const budgets = budgetsText
      ? (parse(budgetsText, { columns: true, skip_empty_lines: true }) as Record<string, string>[]).map(numeric)
      : null;
    return {
      final: final ? { solvers: final.solvers, pairs: final.pairs } : null,
      budgets,
    };
  }
}
