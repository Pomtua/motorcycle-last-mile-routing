import { Algorithm, ALGORITHMS } from '../engine/engine.types';
import { parseClock } from './time';

export const MAX_ZONES = 64;
export const DEFAULT_BUDGET_PER_CUSTOMER = 0.1;
export const DEFAULT_PENALTY_ALPHA = 0.25;

export type JobSettings =
  | { algorithm: 'hgs'; timeBudgetPerCustomer: number; splitMode: 'dynamic' | 'fixed' }
  | { algorithm: 'ortools'; timeLimit: 'match_hgs' | number }
  | { algorithm: 'i1_ls' }
  | { algorithm: 'nn' };

export interface RunRequest {
  datasetId: string;
  seed: number;
  fleet: { size: number; weightCapacityKg: number; volumeCapacityL: number };
  shift: { startS: number; endS: number };
  zoning: { enabled: boolean; numZones: number | null; penaltyAlpha: number };
  jobs: JobSettings[];
}

export class RunRequestError extends Error {}

const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

function fail(message: string): never {
  throw new RunRequestError(message);
}

function object(value: unknown, label: string): Record<string, unknown> {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) {
    fail(`${label} must be an object`);
  }
  return value as Record<string, unknown>;
}

function positive(value: unknown, label: string, max = Number.MAX_SAFE_INTEGER): number {
  if (typeof value !== 'number' || !Number.isFinite(value) || value <= 0 || value > max) {
    fail(`${label} must be a number greater than 0${max < Number.MAX_SAFE_INTEGER ? ` and at most ${max}` : ''}`);
  }
  return value;
}

function integer(value: unknown, label: string, min: number, max: number): number {
  if (typeof value !== 'number' || !Number.isInteger(value) || value < min || value > max) {
    fail(`${label} must be a whole number from ${min} to ${max}`);
  }
  return value;
}

function clock(value: unknown, label: string): number {
  const seconds = typeof value === 'string' ? parseClock(value) : null;
  if (seconds === null) {
    fail(`${label} must be a time like 08:00`);
  }
  return seconds;
}

export function parseRunRequest(body: unknown): RunRequest {
  const root = object(body, 'body');
  if (typeof root.datasetId !== 'string' || !UUID.test(root.datasetId)) {
    fail('datasetId must be a dataset id');
  }
  const seed = root.seed === undefined ? 42 : integer(root.seed, 'seed', 0, 2 ** 32 - 1);

  const fleet = object(root.fleet, 'fleet');
  const shift = object(root.shift, 'shift');
  const startS = clock(shift.start, 'shift.start');
  const endS = clock(shift.end, 'shift.end');
  if (startS >= endS) {
    fail('shift.end must be after shift.start');
  }

  const zoningBody = root.zoning === undefined ? { enabled: false } : object(root.zoning, 'zoning');
  const enabled = zoningBody.enabled === true;
  let numZones: number | null = null;
  if (enabled && zoningBody.numZones !== undefined && zoningBody.numZones !== 'auto') {
    numZones = integer(zoningBody.numZones, 'zoning.numZones', 1, MAX_ZONES);
  }
  const penaltyAlpha =
    zoningBody.penaltyAlpha === undefined
      ? DEFAULT_PENALTY_ALPHA
      : typeof zoningBody.penaltyAlpha === 'number' && Number.isFinite(zoningBody.penaltyAlpha) && zoningBody.penaltyAlpha >= 0
        ? zoningBody.penaltyAlpha
        : fail('zoning.penaltyAlpha must be zero or a positive number');

  if (!Array.isArray(root.algorithms) || root.algorithms.length === 0) {
    fail('select at least one algorithm');
  }
  const jobs: JobSettings[] = [];
  const seen = new Set<Algorithm>();
  for (const [index, entry] of root.algorithms.entries()) {
    const spec = object(entry, `algorithms[${index}]`);
    const algorithm = spec.algorithm as Algorithm;
    if (!ALGORITHMS.includes(algorithm)) {
      fail(`algorithms[${index}].algorithm must be one of ${ALGORITHMS.join(', ')}`);
    }
    if (seen.has(algorithm)) {
      fail(`${algorithm} is selected more than once`);
    }
    seen.add(algorithm);
    if (algorithm === 'hgs') {
      const splitMode = spec.splitMode ?? 'dynamic';
      if (splitMode !== 'dynamic' && splitMode !== 'fixed') {
        fail('splitMode must be dynamic or fixed');
      }
      jobs.push({
        algorithm,
        timeBudgetPerCustomer:
          spec.timeBudgetPerCustomer === undefined
            ? DEFAULT_BUDGET_PER_CUSTOMER
            : positive(spec.timeBudgetPerCustomer, 'timeBudgetPerCustomer', 10),
        splitMode,
      });
    } else if (algorithm === 'ortools') {
      const timeLimit = spec.timeLimit ?? 'match_hgs';
      jobs.push({
        algorithm,
        timeLimit: timeLimit === 'match_hgs' ? 'match_hgs' : positive(timeLimit, 'OR-Tools timeLimit', 3600),
      });
    } else {
      jobs.push({ algorithm });
    }
  }
  const ortools = jobs.find((job) => job.algorithm === 'ortools');
  if (ortools && ortools.timeLimit === 'match_hgs' && !seen.has('hgs')) {
    fail('OR-Tools "Same as C++ Engine" needs the C++ Engine selected; set a custom time limit instead');
  }
  if (!seen.has('nn')) {
    jobs.push({ algorithm: 'nn' });
  }

  return {
    datasetId: root.datasetId,
    seed,
    fleet: {
      size: integer(fleet.size, 'fleet.size', 1, 1000),
      weightCapacityKg: positive(fleet.weightCapacityKg, 'fleet.weightCapacityKg'),
      volumeCapacityL: positive(fleet.volumeCapacityL, 'fleet.volumeCapacityL'),
    },
    shift: { startS, endS },
    zoning: { enabled, numZones, penaltyAlpha },
    jobs,
  };
}
