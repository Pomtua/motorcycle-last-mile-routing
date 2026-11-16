import { onGrid, ParcelError, ParcelSet, VOLUME_STEP_L, WEIGHT_STEP_KG } from './parcels';

export const IMPORT_SHIFT_START_S = 8 * 3600;
const DAY_S = 24 * 3600;

export interface ImportedInstance {
  parcels: ParcelSet;
  size: number;
  durations: Float64Array;
  distances: Float64Array;
  defaults: {
    fleetSize: number;
    weightCapacityKg: number;
    volumeCapacityL: number;
    shiftStartS: number;
    shiftEndS: number;
  };
}

function requireNumber(value: unknown, label: string): number {
  if (typeof value !== 'number' || !Number.isFinite(value)) {
    throw new ParcelError([{ row: 0, message: `${label} must be a finite number` }]);
  }
  return value;
}

function flattenMatrix(value: unknown, size: number, label: string): Float64Array {
  if (!Array.isArray(value) || value.length !== size) {
    throw new ParcelError([{ row: 0, message: `${label} must be a ${size}x${size} matrix` }]);
  }
  const flat = new Float64Array(size * size);
  value.forEach((row: unknown, i) => {
    if (!Array.isArray(row) || row.length !== size) {
      throw new ParcelError([{ row: 0, message: `${label} row ${i} must have ${size} values` }]);
    }
    row.forEach((cell: unknown, j) => {
      const number = requireNumber(cell, `${label}[${i}][${j}]`);
      if (number < 0) {
        throw new ParcelError([{ row: 0, message: `${label}[${i}][${j}] must not be negative` }]);
      }
      flat[i * size + j] = number;
    });
  });
  return flat;
}

export function parseInstanceJson(text: string, maxCustomers: number): ImportedInstance {
  let json: any;
  try {
    json = JSON.parse(text);
  } catch (error) {
    throw new ParcelError([{ row: 0, message: `cannot read the JSON: ${(error as Error).message}` }]);
  }
  const nodes = json?.nodes;
  if (!Array.isArray(nodes) || nodes.length < 2) {
    throw new ParcelError([{ row: 0, message: 'nodes must list the depot followed by at least one customer' }]);
  }
  if (nodes.length - 1 > maxCustomers) {
    throw new ParcelError([{ row: 0, message: `at most ${maxCustomers} customers are supported` }]);
  }
  const horizon = requireNumber(json?.meta?.horizon, 'meta.horizon');
  if (horizon <= 0 || IMPORT_SHIFT_START_S + horizon > DAY_S) {
    throw new ParcelError([{ row: 0, message: 'meta.horizon must fit inside one day starting at 08:00' }]);
  }
  const size = nodes.length;
  const depot = nodes[0];
  const seen = new Set<string>();
  const customers = nodes.slice(1).map((node: any, index: number) => {
    const label = `nodes[${index + 1}]`;
    const externalId = String(node?.osm_id ?? index + 1);
    if (seen.has(externalId)) {
      throw new ParcelError([{ row: 0, message: `${label}.osm_id ${externalId} appears more than once` }]);
    }
    seen.add(externalId);
    const demandWeight = requireNumber(node?.demand_weight, `${label}.demand_weight`);
    const demandVolume = requireNumber(node?.demand_volume, `${label}.demand_volume`);
    const twStart = requireNumber(node?.tw_start, `${label}.tw_start`);
    const twEnd = requireNumber(node?.tw_end, `${label}.tw_end`);
    if (demandWeight < WEIGHT_STEP_KG || !onGrid(demandWeight, WEIGHT_STEP_KG)) {
      throw new ParcelError([{ row: 0, message: `${label}.demand_weight must be at least 0.01 kg with at most 2 decimal places` }]);
    }
    if (demandVolume * 1000 < VOLUME_STEP_L || !onGrid(demandVolume * 1000, VOLUME_STEP_L)) {
      throw new ParcelError([{ row: 0, message: `${label}.demand_volume must be at least 0.0001 m³ with at most 4 decimal places` }]);
    }
    if (twStart > twEnd || twStart < 0) {
      throw new ParcelError([{ row: 0, message: `${label} has an invalid time window` }]);
    }
    return {
      externalId,
      lng: requireNumber(node?.lng, `${label}.lng`),
      lat: requireNumber(node?.lat, `${label}.lat`),
      demandWeight,
      demandVolume,
      twStartS: IMPORT_SHIFT_START_S + twStart,
      twEndS: IMPORT_SHIFT_START_S + twEnd,
      serviceS: requireNumber(node?.service_time ?? 0, `${label}.service_time`),
    };
  });

  return {
    parcels: {
      depot: { lng: requireNumber(depot?.lng, 'nodes[0].lng'), lat: requireNumber(depot?.lat, 'nodes[0].lat') },
      customers,
    },
    size,
    durations: flattenMatrix(json.duration_matrix, size, 'duration_matrix'),
    distances: flattenMatrix(json.distance_matrix, size, 'distance_matrix'),
    defaults: {
      fleetSize: requireNumber(json?.fleet?.size, 'fleet.size'),
      weightCapacityKg: requireNumber(json?.fleet?.weight_capacity, 'fleet.weight_capacity'),
      volumeCapacityL: requireNumber(json?.fleet?.volume_capacity, 'fleet.volume_capacity') * 1000,
      shiftStartS: IMPORT_SHIFT_START_S,
      shiftEndS: IMPORT_SHIFT_START_S + horizon,
    },
  };
}
