import { InstanceJson } from '../engine/engine.types';

export interface CustomerRow {
  id: string;
  node_index: number;
  external_id: string;
  lng: number;
  lat: number;
  demand_weight: number;
  demand_volume: number;
  tw_start_s: number;
  tw_end_s: number;
  service_s: number;
}

export interface BuildSettings {
  seed: number;
  fleetSize: number;
  weightCapacityKg: number;
  volumeCapacityM3: number;
  shiftStartS: number;
  shiftEndS: number;
}

const TIME_TOLERANCE_S = 1e-6;

export interface Unservable {
  externalId: string;
  window: [number, number];
  reason: 'late' | 'return';
  time: number;
}

export function unservableCustomers(
  customers: CustomerRow[],
  matrix: { size: number; durations: Float64Array },
  shiftStartS: number,
  shiftEndS: number,
): Unservable[] {
  const result: Unservable[] = [];
  for (const customer of customers) {
    const i = customer.node_index;
    const arrival = shiftStartS + matrix.durations[i];
    const start = Math.max(arrival, customer.tw_start_s);
    const window: [number, number] = [customer.tw_start_s, customer.tw_end_s];
    if (start > customer.tw_end_s + TIME_TOLERANCE_S) {
      result.push({ externalId: customer.external_id, window, reason: 'late', time: arrival });
      continue;
    }
    const back = start + customer.service_s + matrix.durations[i * matrix.size];
    if (back > shiftEndS + TIME_TOLERANCE_S) {
      result.push({ externalId: customer.external_id, window, reason: 'return', time: back });
    }
  }
  return result;
}

export function buildInstance(
  depot: { lng: number; lat: number },
  customers: CustomerRow[],
  matrix: { size: number; durations: Float64Array; distances: Float64Array },
  settings: BuildSettings,
): InstanceJson {
  const ordered = [...customers].sort((a, b) => a.node_index - b.node_index);
  if (matrix.size !== ordered.length + 1) {
    throw new Error(`matrix size ${matrix.size} does not match ${ordered.length} customers plus the depot`);
  }
  ordered.forEach((customer, index) => {
    if (customer.node_index !== index + 1) {
      throw new Error(`customer node indices must be 1..${ordered.length}`);
    }
  });
  const horizon = settings.shiftEndS - settings.shiftStartS;
  const toRows = (flat: Float64Array) =>
    Array.from({ length: matrix.size }, (_, i) => Array.from(flat.subarray(i * matrix.size, (i + 1) * matrix.size)));

  return {
    meta: {
      seed: settings.seed,
      n: ordered.length,
      horizon,
      w_max: settings.weightCapacityKg,
      v_max: settings.volumeCapacityM3,
    },
    fleet: {
      size: settings.fleetSize,
      weight_capacity: settings.weightCapacityKg,
      volume_capacity: settings.volumeCapacityM3,
    },
    nodes: [
      {
        osm_id: 'depot',
        type: 'depot',
        lng: depot.lng,
        lat: depot.lat,
        demand_weight: 0,
        demand_volume: 0,
        tw_start: 0,
        tw_end: horizon,
        service_time: 0,
      },
      ...ordered.map((customer) => ({
        osm_id: customer.external_id,
        type: 'customer',
        lng: customer.lng,
        lat: customer.lat,
        demand_weight: customer.demand_weight,
        demand_volume: customer.demand_volume,
        tw_start: Math.max(0, Math.round(customer.tw_start_s - settings.shiftStartS)),
        tw_end: Math.min(horizon, Math.round(customer.tw_end_s - settings.shiftStartS)),
        service_time: customer.service_s,
      })),
    ],
    duration_matrix: toRows(matrix.durations),
    distance_matrix: toRows(matrix.distances),
  };
}

export function encodeMatrix(values: Float64Array): Buffer {
  return Buffer.from(values.buffer, values.byteOffset, values.byteLength);
}

export function decodeMatrix(bytes: Buffer): Float64Array {
  const copy = new Uint8Array(bytes.byteLength);
  copy.set(bytes);
  return new Float64Array(copy.buffer);
}
