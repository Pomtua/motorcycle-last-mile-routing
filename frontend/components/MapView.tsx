'use client';

import L from 'leaflet';
import { useEffect, useMemo, useState } from 'react';
import { CircleMarker, GeoJSON, MapContainer, Marker, Polyline, Popup, TileLayer, useMap } from 'react-leaflet';
import { DatasetDetail, JobResult, ResultRoute } from '@/lib/api';
import { clock, clockToSeconds, km, minutes, riderColor, zoneColor } from '@/lib/format';

interface MapViewProps {
  dataset: DatasetDetail | null;
  result: JobResult | null;
  zoning: boolean;
}

const BANGKOK: [number, number] = [13.7563, 100.5018];

const depotIcon = L.divIcon({ className: '', html: '<div class="depot-marker" style="width:22px;height:22px"></div>', iconSize: [22, 22], iconAnchor: [11, 11] });

function stopIcon(sequence: number, color: string, split: boolean): L.DivIcon {
  const ring = split ? `box-shadow:0 0 0 2px #fff,0 0 0 4px ${color};` : '';
  return L.divIcon({
    className: '',
    html: `<div class="stop-marker" style="width:20px;height:20px;border:2.5px solid ${color};${ring}">${sequence}</div>`,
    iconSize: [20, 20],
    iconAnchor: [10, 10],
  });
}

function FitBounds({ points, signature }: { points: [number, number][]; signature: string }) {
  const map = useMap();
  useEffect(() => {
    if (points.length === 0) {
      return;
    }
    map.fitBounds(L.latLngBounds(points), { padding: [40, 40], maxZoom: 15 });
  }, [map, signature]);
  return null;
}

function routePositions(route: ResultRoute, depot: { lng: number; lat: number }): [number, number][] {
  if (route.path) {
    return route.path.coordinates.map(([lng, lat]) => [lat, lng]);
  }
  return [[depot.lat, depot.lng], ...route.stops.map((stop) => [stop.lat, stop.lng] as [number, number]), [depot.lat, depot.lng]];
}

export default function MapView({ dataset, result, zoning }: MapViewProps) {
  const [hidden, setHidden] = useState<Set<number>>(new Set());
  const [highlight, setHighlight] = useState<number | null>(null);
  const [showZones, setShowZones] = useState(true);

  const resultMatches = !!result && !!dataset && result.job.dataset_id === dataset.id;
  const shownResult = resultMatches ? result : null;
  const jobId = shownResult?.job.id ?? null;

  useEffect(() => {
    setHidden(new Set());
    setHighlight(null);
  }, [jobId]);

  const depot = shownResult?.job.depot ?? dataset?.depot ?? null;
  const points = useMemo<[number, number][]>(() => {
    const list: [number, number][] = [];
    if (depot) {
      list.push([depot.lat, depot.lng]);
    }
    dataset?.customers.forEach((customer) => list.push([customer.lat, customer.lng]));
    return list;
  }, [dataset, depot]);

  const shiftStart = shownResult ? clockToSeconds(shownResult.job.shift_start) : 0;
  const hasZones = !!shownResult && shownResult.zones.length > 0;

  return (
    <div className="relative h-full w-full">
      <MapContainer center={BANGKOK} zoom={12} className="h-full w-full" preferCanvas={false}>
        <TileLayer
          attribution='&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
          url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
        />
        <FitBounds points={points} signature={`${dataset?.id ?? ''}:${points.length}`} />

        {shownResult && showZones &&
          shownResult.zones.map((zone) => (
            <GeoJSON
              key={`${jobId}-zone-${zone.zone_index}`}
              data={zone.hull}
              style={{
                color: zoneColor(zone.zone_index),
                weight: 2,
                dashArray: '6 6',
                fillColor: zoneColor(zone.zone_index),
                fillOpacity: 0.12,
              }}
            />
          ))}

        {shownResult &&
          shownResult.routes.map((route) => {
            if (hidden.has(route.vehicle_index)) {
              return null;
            }
            const dim = highlight !== null && highlight !== route.vehicle_index;
            return (
              <Polyline
                key={`${jobId}-route-${route.vehicle_index}`}
                positions={routePositions(route, shownResult.job.depot)}
                pathOptions={{
                  color: riderColor(route.vehicle_index),
                  weight: highlight === route.vehicle_index ? 6 : 4,
                  opacity: dim ? 0.15 : 0.9,
                }}
              />
            );
          })}

        {shownResult &&
          shownResult.routes.flatMap((route) =>
            hidden.has(route.vehicle_index) || (highlight !== null && highlight !== route.vehicle_index)
              ? []
              : route.stops.map((stop) => (
                  <Marker
                    key={`${jobId}-${route.vehicle_index}-${stop.sequence}`}
                    position={[stop.lat, stop.lng]}
                    icon={stopIcon(stop.sequence, riderColor(route.vehicle_index), stop.split_count > 1)}
                  >
                    <Popup>
                      <div className="space-y-0.5 text-[13px]">
                        <p className="font-semibold">
                          Parcel {stop.external_id}
                          <span className="ml-1 font-normal text-muted">· Rider #{route.vehicle_index + 1}, stop {stop.sequence}</span>
                        </p>
                        <p>
                          Window {stop.tw_start}–{stop.tw_end} · ETA {clock(shiftStart + stop.arrival_s)}
                          {stop.service_start_s > stop.arrival_s + 1 && ` (waits until ${clock(shiftStart + stop.service_start_s)})`}
                        </p>
                        <p>
                          Delivered {stop.delivered_weight.toFixed(2)} kg · {stop.delivered_volume_l.toFixed(2)} L
                        </p>
                        {stop.zone_index !== null && <p>Zone {stop.zone_index + 1}</p>}
                        {stop.split_count > 1 && (
                          <p className="font-semibold text-primary">Split delivery: {stop.split_count} riders</p>
                        )}
                      </div>
                    </Popup>
                  </Marker>
                )),
          )}

        {!shownResult &&
          dataset?.customers.map((customer) => (
            <CircleMarker
              key={customer.id}
              center={[customer.lat, customer.lng]}
              radius={5}
              pathOptions={{ color: '#1d4ed8', weight: 2, fillColor: '#ffffff', fillOpacity: 1 }}
            >
              <Popup>
                <div className="text-[13px]">
                  <p className="font-semibold">Parcel {customer.external_id}</p>
                  <p>
                    Window {customer.tw_start}–{customer.tw_end} · service {customer.service_min.toFixed(1)} min
                  </p>
                  <p>
                    {customer.weight_kg.toFixed(2)} kg · {customer.volume_l.toFixed(2)} L
                  </p>
                  {customer.snap_distance_m !== null && (
                    <p className="text-muted">Moved {Math.round(customer.snap_distance_m)} m onto the nearest road</p>
                  )}
                </div>
              </Popup>
            </CircleMarker>
          ))}

        {depot && (
          <Marker position={[depot.lat, depot.lng]} icon={depotIcon} zIndexOffset={1000}>
            <Popup>Depot</Popup>
          </Marker>
        )}
      </MapContainer>

      {shownResult && (
        <div className="absolute top-4 right-4 z-[1000] w-56 rounded-xl bg-white/95 p-4 shadow-lg ring-1 ring-line">
          <div className="mb-2 flex items-center justify-between">
            <h4 className="text-[15px] font-semibold">Active Riders</h4>
            <div className="flex gap-2 text-xs font-medium text-primary">
              <button onClick={() => setHidden(new Set())}>All</button>
              <button onClick={() => setHidden(new Set(shownResult.routes.map((route) => route.vehicle_index)))}>None</button>
            </div>
          </div>
          <ul className="max-h-64 space-y-1 overflow-y-auto pr-1">
            {shownResult.routes.map((route) => {
              const off = hidden.has(route.vehicle_index);
              return (
                <li key={route.vehicle_index}>
                  <button
                    className={`flex w-full items-center gap-2 rounded-md px-1 py-0.5 text-left text-sm hover:bg-zebra ${off ? 'opacity-40' : ''}`}
                    onClick={() =>
                      setHidden((set) => {
                        const next = new Set(set);
                        if (next.has(route.vehicle_index)) {
                          next.delete(route.vehicle_index);
                        } else {
                          next.add(route.vehicle_index);
                        }
                        return next;
                      })
                    }
                    onMouseEnter={() => !off && setHighlight(route.vehicle_index)}
                    onMouseLeave={() => setHighlight(null)}
                    title={`${route.stops.length} stops · ${km(route.distance_m)} · ${minutes(route.duration_s)}`}
                  >
                    <span className="h-1.5 w-7 shrink-0 rounded-full" style={{ background: riderColor(route.vehicle_index) }} />
                    <span className="flex-1">Rider #{route.vehicle_index + 1}</span>
                    <span className="text-xs text-muted">{route.stops.length}</span>
                  </button>
                </li>
              );
            })}
          </ul>
          <label className={`mt-3 flex items-center gap-2 border-t border-line pt-3 text-sm ${hasZones ? '' : 'opacity-40'}`}>
            <input
              type="checkbox"
              checked={hasZones && showZones}
              disabled={!hasZones}
              onChange={(event) => setShowZones(event.target.checked)}
              className="h-4 w-4 accent-primary"
            />
            Show Zone
            {!hasZones && <span className="text-xs text-muted">{zoning ? '' : '(zoning off)'}</span>}
          </label>
        </div>
      )}
      {!dataset && (
        <div className="pointer-events-none absolute inset-0 z-[1000] flex items-center justify-center">
          <p className="rounded-lg bg-white/90 px-4 py-3 text-sm text-muted shadow">Upload a parcel CSV to start.</p>
        </div>
      )}
    </div>
  );
}
