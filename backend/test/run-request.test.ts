import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import { parseRunRequest, RunRequestError } from '../src/domain/run-request';

const DATASET = '8496f6eb-509c-4cd3-8408-9c80af248412';

function body(overrides: Record<string, unknown> = {}) {
  return {
    datasetId: DATASET,
    fleet: { size: 5, weightCapacityKg: 20, volumeCapacityL: 50 },
    shift: { start: '08:00', end: '16:00' },
    algorithms: [{ algorithm: 'hgs' }],
    ...overrides,
  };
}

function rejects(value: unknown, pattern: RegExp) {
  assert.throws(() => parseRunRequest(value), (error: unknown) => {
    assert.ok(error instanceof RunRequestError);
    assert.match(error.message, pattern);
    return true;
  });
}

describe('parseRunRequest', () => {
  it('fills defaults and always adds the NN baseline', () => {
    const request = parseRunRequest(body());
    assert.equal(request.seed, 42);
    assert.deepEqual(request.shift, { startS: 8 * 3600, endS: 16 * 3600 });
    assert.deepEqual(request.zoning, { enabled: false, numZones: null, penaltyAlpha: 0.25 });
    assert.deepEqual(request.jobs, [
      { algorithm: 'hgs', timeBudgetPerCustomer: 0.1, splitMode: 'dynamic' },
      { algorithm: 'nn' },
    ]);
  });

  it('keeps NN once when it is selected', () => {
    const request = parseRunRequest(body({ algorithms: [{ algorithm: 'nn' }, { algorithm: 'i1_ls' }] }));
    assert.deepEqual(
      request.jobs.map((job) => job.algorithm),
      ['nn', 'i1_ls'],
    );
  });

  it('reads per-algorithm settings', () => {
    const request = parseRunRequest(
      body({
        seed: 7,
        zoning: { enabled: true, numZones: 6, penaltyAlpha: 0.5 },
        algorithms: [
          { algorithm: 'hgs', timeBudgetPerCustomer: 0.05, splitMode: 'fixed' },
          { algorithm: 'ortools', timeLimit: 'match_hgs' },
        ],
      }),
    );
    assert.equal(request.seed, 7);
    assert.deepEqual(request.zoning, { enabled: true, numZones: 6, penaltyAlpha: 0.5 });
    assert.deepEqual(request.jobs.slice(0, 2), [
      { algorithm: 'hgs', timeBudgetPerCustomer: 0.05, splitMode: 'fixed' },
      { algorithm: 'ortools', timeLimit: 'match_hgs' },
    ]);
  });

  it('treats auto zones as no fixed count', () => {
    const request = parseRunRequest(body({ zoning: { enabled: true, numZones: 'auto' } }));
    assert.equal(request.zoning.numZones, null);
  });

  it('rejects invalid requests with a clear message', () => {
    rejects(null, /body must be an object/);
    rejects(body({ datasetId: 'x' }), /datasetId/);
    rejects(body({ fleet: { size: 0, weightCapacityKg: 20, volumeCapacityL: 50 } }), /fleet.size/);
    rejects(body({ fleet: { size: 2, weightCapacityKg: -1, volumeCapacityL: 50 } }), /weightCapacityKg/);
    rejects(body({ shift: { start: '16:00', end: '08:00' } }), /shift.end must be after/);
    rejects(body({ shift: { start: '8am', end: '16:00' } }), /shift.start/);
    rejects(body({ algorithms: [] }), /at least one algorithm/);
    rejects(body({ algorithms: [{ algorithm: 'tabu' }] }), /must be one of/);
    rejects(body({ algorithms: [{ algorithm: 'hgs' }, { algorithm: 'hgs' }] }), /more than once/);
    rejects(body({ algorithms: [{ algorithm: 'hgs', splitMode: 'half' }] }), /splitMode/);
    rejects(body({ algorithms: [{ algorithm: 'hgs', timeBudgetPerCustomer: 11 }] }), /timeBudgetPerCustomer/);
    rejects(body({ algorithms: [{ algorithm: 'ortools' }] }), /needs the C\+\+ Engine/);
    rejects(body({ zoning: { enabled: true, numZones: 65 } }), /numZones/);
    rejects(body({ zoning: { enabled: true, penaltyAlpha: -1 } }), /penaltyAlpha/);
    rejects(body({ seed: -1 }), /seed/);
  });

  it('accepts a custom OR-Tools limit without the C++ Engine', () => {
    const request = parseRunRequest(body({ algorithms: [{ algorithm: 'ortools', timeLimit: 12.5 }] }));
    assert.deepEqual(request.jobs[0], { algorithm: 'ortools', timeLimit: 12.5 });
  });
});
