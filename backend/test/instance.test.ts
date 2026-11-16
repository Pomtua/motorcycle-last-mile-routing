import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import { buildInstance, CustomerRow, decodeMatrix, encodeMatrix, unservableCustomers } from '../src/domain/instance-builder';
import { parseInstanceJson } from '../src/domain/instance-import';
import { ParcelError } from '../src/domain/parcels';
import { formatClock, parseClock } from '../src/domain/time';

function customer(index: number, start: string, end: string): CustomerRow {
  return {
    id: String(100 + index),
    node_index: index,
    external_id: `P${index}`,
    lng: 100.5 + index / 100,
    lat: 13.7,
    demand_weight: 2,
    demand_volume: 0.004,
    tw_start_s: parseClock(start) as number,
    tw_end_s: parseClock(end) as number,
    service_s: 120,
  };
}

const matrix = {
  size: 3,
  durations: Float64Array.from([0, 10, 20, 10, 0, 30, 20, 30, 0]),
  distances: Float64Array.from([0, 100, 200, 100, 0, 300, 200, 300, 0]),
};

describe('clock helpers', () => {
  it('parses and formats times of day', () => {
    assert.equal(parseClock('08:30'), 30600);
    assert.equal(parseClock('23:59:59'), 86399);
    assert.equal(parseClock('24:00'), null);
    assert.equal(parseClock('8:5'), null);
    assert.equal(formatClock(30600), '08:30:00');
  });
});

describe('buildInstance', () => {
  it('builds the engine instance relative to the shift start', () => {
    const instance = buildInstance({ lng: 100.5, lat: 13.7 }, [customer(2, '07:00', '09:00'), customer(1, '09:00', '17:00')], matrix, {
      seed: 42,
      fleetSize: 3,
      weightCapacityKg: 20,
      volumeCapacityM3: 0.05,
      shiftStartS: 8 * 3600,
      shiftEndS: 16 * 3600,
    });
    assert.equal(instance.meta.n, 2);
    assert.equal(instance.meta.horizon, 8 * 3600);
    assert.deepEqual(instance.fleet, { size: 3, weight_capacity: 20, volume_capacity: 0.05 });
    assert.equal(instance.nodes[0].type, 'depot');
    assert.equal(instance.nodes[1].osm_id, 'P1');
    assert.equal(instance.nodes[1].tw_start, 3600);
    assert.equal(instance.nodes[1].tw_end, 8 * 3600);
    assert.equal(instance.nodes[2].tw_start, 0);
    assert.equal(instance.nodes[2].tw_end, 3600);
    assert.deepEqual(instance.duration_matrix, [
      [0, 10, 20],
      [10, 0, 30],
      [20, 30, 0],
    ]);
  });

  it('rejects a matrix that does not match the customers', () => {
    assert.throws(
      () =>
        buildInstance({ lng: 100.5, lat: 13.7 }, [customer(1, '09:00', '10:00')], matrix, {
          seed: 1,
          fleetSize: 1,
          weightCapacityKg: 1,
          volumeCapacityM3: 1,
          shiftStartS: 0,
          shiftEndS: 3600,
        }),
      /matrix size 3/,
    );
  });

  it('finds parcels no rider can serve within the shift', () => {
    const shiftStart = 8 * 3600;
    const shiftEnd = 16 * 3600;
    const travel = {
      size: 3,
      durations: Float64Array.from([0, 600, 1200, 600, 0, 300, 900, 300, 0]),
    };
    const late = customer(1, '07:00', '08:05');
    const fine = customer(2, '09:00', '10:00');
    assert.deepEqual(unservableCustomers([late, fine], travel, shiftStart, shiftEnd), [
      { externalId: 'P1', window: [7 * 3600, 8 * 3600 + 300], reason: 'late', time: shiftStart + 600 },
    ]);

    const onTime = customer(1, '07:00', '08:10');
    assert.deepEqual(unservableCustomers([onTime], travel, shiftStart, shiftEnd), []);

    const tooLate = customer(2, '15:50', '17:00');
    assert.deepEqual(unservableCustomers([tooLate], travel, shiftStart, shiftEnd), [
      { externalId: 'P2', window: [15 * 3600 + 3000, 17 * 3600], reason: 'return', time: 15 * 3600 + 3000 + 120 + 900 },
    ]);

    const outside = customer(1, '06:00', '07:30');
    assert.equal(unservableCustomers([outside], travel, shiftStart, shiftEnd)[0].reason, 'late');
  });

  it('round-trips matrices through bytea encoding', () => {
    const decoded = decodeMatrix(encodeMatrix(matrix.distances));
    assert.deepEqual(Array.from(decoded), Array.from(matrix.distances));
  });
});

describe('parseInstanceJson', () => {
  const instance = {
    meta: { seed: 42, n: 2, horizon: 28800 },
    fleet: { size: 3, weight_capacity: 50, volume_capacity: 0.125 },
    nodes: [
      { osm_id: 'D', lng: 100.5, lat: 13.7, demand_weight: 0, demand_volume: 0, tw_start: 0, tw_end: 28800, service_time: 0 },
      { osm_id: 'A', lng: 100.51, lat: 13.71, demand_weight: 2, demand_volume: 0.01, tw_start: 0, tw_end: 3600, service_time: 120 },
      { osm_id: 'B', lng: 100.52, lat: 13.72, demand_weight: 3, demand_volume: 0.02, tw_start: 1800, tw_end: 7200, service_time: 60 },
    ],
    duration_matrix: [
      [0, 10, 20],
      [10, 0, 30],
      [20, 30, 0],
    ],
    distance_matrix: [
      [0, 100, 200],
      [100, 0, 300],
      [200, 300, 0],
    ],
  };

  it('maps a benchmark instance onto an 08:00 shift and keeps its matrices', () => {
    const imported = parseInstanceJson(JSON.stringify(instance), 1999);
    assert.equal(imported.size, 3);
    assert.deepEqual(imported.parcels.depot, { lng: 100.5, lat: 13.7 });
    assert.equal(imported.parcels.customers[1].twStartS, 8 * 3600 + 1800);
    assert.deepEqual(imported.defaults, {
      fleetSize: 3,
      weightCapacityKg: 50,
      volumeCapacityL: 125,
      shiftStartS: 8 * 3600,
      shiftEndS: 16 * 3600,
    });
    assert.equal(imported.distances[1 * 3 + 2], 300);
  });

  it('rejects broken instances', () => {
    assert.throws(() => parseInstanceJson('{', 1999), ParcelError);
    assert.throws(() => parseInstanceJson(JSON.stringify({ ...instance, distance_matrix: [[0]] }), 1999), /distance_matrix/);
    const negative = structuredClone(instance);
    negative.duration_matrix[0][1] = -1;
    assert.throws(() => parseInstanceJson(JSON.stringify(negative), 1999), /must not be negative/);
    const duplicate = structuredClone(instance);
    duplicate.nodes[2].osm_id = 'A';
    assert.throws(() => parseInstanceJson(JSON.stringify(duplicate), 1999), /more than once/);
    const fineWeight = structuredClone(instance);
    fineWeight.nodes[1].demand_weight = 2.345;
    assert.throws(() => parseInstanceJson(JSON.stringify(fineWeight), 1999), /demand_weight must be at least 0.01 kg/);
    const fineVolume = structuredClone(instance);
    fineVolume.nodes[1].demand_volume = 0.00005;
    assert.throws(() => parseInstanceJson(JSON.stringify(fineVolume), 1999), /demand_volume must be at least 0.0001/);
  });
});
