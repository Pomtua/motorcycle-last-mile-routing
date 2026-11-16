'use client';

import { useEffect, useState } from 'react';
import { api, JobResult, ResultRoute } from '@/lib/api';
import { clock, clockToSeconds, km, minutes, riderColor } from '@/lib/format';

function Panel({ title, action, children }: { title: string; action?: React.ReactNode; children: React.ReactNode }) {
  return (
    <div className="rounded-xl border border-line bg-white px-8 py-6">
      <div className="mb-4 flex items-center justify-between">
        <h3 className="text-[15px] font-semibold text-ink">{title}</h3>
        {action}
      </div>
      {children}
    </div>
  );
}

function Usage({ used, capacity, unit }: { used: number; capacity: number; unit: string }) {
  const share = capacity > 0 ? Math.min(1, used / capacity) : 0;
  return (
    <div className="mx-auto w-32">
      <div className="tabular-nums">
        {used.toFixed(1)} / {Number.isInteger(capacity) ? capacity : capacity.toFixed(1)}
        <span className="sr-only"> {unit}</span>
      </div>
      <div className="mt-1 h-1.5 overflow-hidden rounded-full bg-line">
        <div
          className={`h-full rounded-full ${share > 0.999 ? 'bg-accent-orange' : 'bg-accent-blue'}`}
          style={{ width: `${share * 100}%` }}
        />
      </div>
    </div>
  );
}

function Timeline({ routes, horizon, selected, onSelect }: { routes: ResultRoute[]; horizon: number; selected: number; onSelect: (index: number) => void }) {
  const pct = (seconds: number) => `${(Math.max(0, Math.min(horizon, seconds)) / horizon) * 100}%`;
  const width = (from: number, to: number) => `${(Math.max(0, Math.min(horizon, to) - Math.max(0, from)) / horizon) * 100}%`;
  const step = horizon <= 2 * 3600 ? 900 : horizon <= 4 * 3600 ? 1800 : 3600;
  const ticks = Array.from({ length: Math.floor(horizon / step) + 1 }, (_, index) => index * step);
  return (
    <div>
      <div className="mb-2 flex items-center gap-4 text-xs text-muted">
        <span className="flex items-center gap-1.5">
          <span className="h-2.5 w-5 rounded-sm bg-slate-300" /> travel
        </span>
        <span className="flex items-center gap-1.5">
          <span className="h-2.5 w-5 rounded-sm bg-[repeating-linear-gradient(45deg,#f59e0b_0,#f59e0b_2px,#fef3c7_2px,#fef3c7_5px)]" /> waiting for window
        </span>
        <span className="flex items-center gap-1.5">
          <span className="h-2.5 w-5 rounded-sm bg-accent-blue" /> delivering
        </span>
      </div>
      <div className="max-h-[360px] overflow-y-auto pr-1">
        {routes.map((route, index) => {
          const color = riderColor(route.vehicle_index);
          let previous = 0;
          const segments: React.ReactNode[] = [];
          route.stops.forEach((stop) => {
            segments.push(
              <div key={`t${stop.sequence}`} className="absolute top-1 bottom-1 bg-slate-300" style={{ left: pct(previous), width: width(previous, stop.arrival_s) }} />,
            );
            if (stop.service_start_s > stop.arrival_s) {
              segments.push(
                <div
                  key={`w${stop.sequence}`}
                  className="absolute top-1 bottom-1 bg-[repeating-linear-gradient(45deg,#f59e0b_0,#f59e0b_2px,#fef3c7_2px,#fef3c7_5px)]"
                  style={{ left: pct(stop.arrival_s), width: width(stop.arrival_s, stop.service_start_s) }}
                />,
              );
            }
            segments.push(
              <div
                key={`s${stop.sequence}`}
                className="absolute top-0.5 bottom-0.5 rounded-[2px]"
                style={{ left: pct(stop.service_start_s), width: width(stop.service_start_s, stop.departure_s), background: color, minWidth: 2 }}
                title={`${stop.external_id} ${stop.tw_start}–${stop.tw_end}`}
              />,
            );
            previous = stop.departure_s;
          });
          segments.push(
            <div key="return" className="absolute top-1 bottom-1 bg-slate-300" style={{ left: pct(previous), width: width(previous, route.duration_s) }} />,
          );
          return (
            <button
              key={route.vehicle_index}
              onClick={() => onSelect(index)}
              className={`flex w-full items-center gap-3 rounded-md px-1 py-1 text-left ${index === selected ? 'bg-primary-tint' : 'hover:bg-zebra'}`}
            >
              <span className="w-20 shrink-0 text-sm">Driver {route.vehicle_index + 1}</span>
              <span className="relative h-5 flex-1 rounded bg-zebra ring-1 ring-line">{segments}</span>
            </button>
          );
        })}
      </div>
      <div className="relative mt-1 ml-[92px] h-4 text-[11px] text-muted">
        {ticks.map((tick) => (
          <span key={tick} className="absolute -translate-x-1/2" style={{ left: pct(tick) }}>
            +{tick >= 3600 && tick % 3600 === 0 ? `${tick / 3600}h` : `${Math.round(tick / 60)}m`}
          </span>
        ))}
      </div>
    </div>
  );
}

export default function FleetSchedule({ result, loading }: { result: JobResult | null; loading: boolean }) {
  const [selected, setSelected] = useState(0);
  const jobId = result?.job.id;

  useEffect(() => setSelected(0), [jobId]);

  if (!result) {
    return (
      <div className="rounded-xl border border-line bg-white px-8 py-16 text-center text-sm text-muted">
        {loading ? 'Loading the schedule…' : 'Run a simulation to see each driver’s schedule.'}
      </div>
    );
  }

  const shiftStart = clockToSeconds(result.job.shift_start);
  const shiftLength = clockToSeconds(result.job.shift_end) - shiftStart;
  const latestReturn = Math.max(0, ...result.routes.map((item) => item.duration_s));
  const horizon = Math.min(shiftLength, Math.max(1800, Math.ceil(latestReturn / 1800) * 1800));
  const route = result.routes[selected] ?? result.routes[0];

  return (
    <div className="flex flex-col gap-4">
      <Panel
        title="Route Metrics per Driver"
        action={
          <a
            href={api.exportUrl(result.job.id)}
            className="rounded-lg border border-line px-3 py-1.5 text-sm font-medium text-primary hover:bg-primary-tint"
          >
            Download routes (CSV)
          </a>
        }
      >
        <div className="max-h-[420px] overflow-auto">
          <table className="data-table">
            <thead className="sticky top-0">
              <tr>
                <th>Driver</th>
                <th>Stops</th>
                <th>Total Weight (kg)</th>
                <th>Total Volume (L)</th>
                <th>Total Distance</th>
                <th>Total Travel Time</th>
                <th>Return Time</th>
              </tr>
            </thead>
            <tbody>
              {result.routes.map((item, index) => (
                <tr
                  key={item.vehicle_index}
                  className={`clickable ${index === selected ? 'selected' : ''}`}
                  onClick={() => setSelected(index)}
                >
                  <td>
                    <span className="inline-flex items-center gap-2">
                      <span className="h-2.5 w-2.5 rounded-full" style={{ background: riderColor(item.vehicle_index) }} />
                      Driver {item.vehicle_index + 1}
                    </span>
                  </td>
                  <td>{item.stops.length}</td>
                  <td>
                    <Usage used={item.load_weight} capacity={result.job.weight_capacity} unit="kg" />
                  </td>
                  <td>
                    <Usage used={item.load_volume_l} capacity={result.job.volume_capacity_l} unit="L" />
                  </td>
                  <td>{km(item.distance_m)}</td>
                  <td>{minutes(item.duration_s)}</td>
                  <td>{clock(shiftStart + item.duration_s)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </Panel>

      <Panel title={`Timeline (${result.job.shift_start}–${clock(shiftStart + horizon)}, shift ends ${result.job.shift_end})`}>
        <Timeline routes={result.routes} horizon={horizon} selected={selected} onSelect={setSelected} />
      </Panel>

      {route && (
        <Panel title={`Detailed Sequence - Driver ${route.vehicle_index + 1}`}>
          <table className="data-table">
            <thead>
              <tr>
                <th>Sequence</th>
                <th>Delivery Point (ID)</th>
                <th>Time Window</th>
                <th>ETA</th>
                <th>Delivered</th>
                <th>Status</th>
              </tr>
            </thead>
            <tbody>
              <tr>
                <td>0</td>
                <td>Depot</td>
                <td>-</td>
                <td>{result.job.shift_start}</td>
                <td>-</td>
                <td>Start</td>
              </tr>
              {route.stops.map((stop) => {
                const waited = stop.service_start_s - stop.arrival_s;
                return (
                  <tr key={stop.sequence}>
                    <td>{stop.sequence}</td>
                    <td>{stop.external_id}</td>
                    <td>
                      {stop.tw_start} - {stop.tw_end}
                    </td>
                    <td>{clock(shiftStart + stop.arrival_s)}</td>
                    <td>
                      {stop.delivered_weight.toFixed(2)} kg · {stop.delivered_volume_l.toFixed(1)} L
                      {stop.split_count > 1 && (
                        <span className="ml-2 rounded bg-primary-tint px-1.5 py-0.5 text-xs font-semibold text-primary">
                          split ×{stop.split_count}
                        </span>
                      )}
                    </td>
                    <td className={stop.on_time ? '' : 'font-semibold text-accent-red'}>
                      {!stop.on_time ? 'Late' : waited >= 60 ? `Waited ${Math.round(waited / 60)} min` : 'On Time'}
                    </td>
                  </tr>
                );
              })}
              <tr>
                <td>{route.stops.length + 1}</td>
                <td>Depot</td>
                <td>-</td>
                <td>{clock(shiftStart + route.duration_s)}</td>
                <td>-</td>
                <td>Finish</td>
              </tr>
            </tbody>
          </table>
        </Panel>
      )}
    </div>
  );
}
