import { ParcelError, RowError } from './parcels';

export const DEFAULT_MAX_SNAP_DISTANCE_M = 200;
export const MAX_SNAP_DISTANCE_LIMIT_M = 100_000;

export interface SnapPoint {
  row: number;
  label: string;
  distance: number;
}

export function parseMaxSnapDistance(raw: unknown): number {
  if (raw === undefined || raw === null || raw === '') {
    return DEFAULT_MAX_SNAP_DISTANCE_M;
  }
  const value = typeof raw === 'number' ? raw : Number(raw);
  if (!Number.isFinite(value) || value <= 0 || value > MAX_SNAP_DISTANCE_LIMIT_M) {
    throw new ParcelError([
      { row: 0, message: `maxSnapDistanceM must be a number of metres above 0 and at most ${MAX_SNAP_DISTANCE_LIMIT_M}` },
    ]);
  }
  return value;
}

export function snapErrors(points: SnapPoint[], limit: number): RowError[] {
  return points
    .filter((point) => point.distance > limit)
    .map((point) => ({
      row: point.row,
      message: `${point.label} is ${Math.round(point.distance)} m from the nearest road (limit ${limit} m)`,
    }));
}
