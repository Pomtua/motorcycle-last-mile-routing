"use client";

import { MapContainer, TileLayer, Polyline, Marker, Popup, Circle, Polygon } from "react-leaflet";
import "leaflet/dist/leaflet.css";
import L from "leaflet";
import { useState, useEffect } from "react";
import { ChevronUp, ChevronDown } from "lucide-react";

const DefaultIcon = L.icon({
  iconUrl: "https://unpkg.com/leaflet@1.9.4/dist/images/marker-icon.png",
  shadowUrl: "https://unpkg.com/leaflet@1.9.4/dist/images/marker-shadow.png",
  iconSize: [25, 41],
  iconAnchor: [12, 41],
});
L.Marker.prototype.options.icon = DefaultIcon;

const routeColors = ["#3b82f6", "#22c55e", "#ef4444", "#eab308", "#a855f7"];

function crossProduct(a: { lat: number; lng: number }, b: { lat: number; lng: number }, c: { lat: number; lng: number }) {
  return (b.lng - a.lng) * (c.lat - a.lat) - (b.lat - a.lat) * (c.lng - a.lng);
}

function getConvexHull(points: { lat: number; lng: number }[]) {
  if (points.length < 3) return points.map(p => [p.lat, p.lng] as [number, number]);

  const sorted = [...points].sort((a, b) => a.lng !== b.lng ? a.lng - b.lng : a.lat - b.lat);

  const lower: { lat: number; lng: number }[] = [];
  for (const p of sorted) {
    while (lower.length >= 2 && crossProduct(lower[lower.length - 2], lower[lower.length - 1], p) <= 0) {
      lower.pop();
    }
    lower.push(p);
  }

  const upper: { lat: number; lng: number }[] = [];
  for (let i = sorted.length - 1; i >= 0; i--) {
    const p = sorted[i];
    while (upper.length >= 2 && crossProduct(upper[upper.length - 2], upper[upper.length - 1], p) <= 0) {
      upper.pop();
    }
    upper.push(p);
  }

  upper.pop();
  lower.pop();
  return lower.concat(upper).map(p => [p.lat, p.lng] as [number, number]);
}

export default function MapView({ 
  routes, 
  showZone, 
  setShowZone,
  parcels, 
  depot 
}: { 
  routes: any[], 
  showZone: boolean, 
  setShowZone: (val: boolean) => void,
  parcels: any[], 
  depot: any 
}) {
  const [isExpanded, setIsExpanded] = useState(true);
  const center: [number, number] = depot ? [depot.lat, depot.lng] : [13.7563, 100.5018];

  return (
    <div className="relative w-full h-full rounded-xl overflow-hidden border border-gray-200">
      <MapContainer center={center} zoom={11} className="w-full h-full">
        <TileLayer
          attribution='&copy; <a href="https://www.openstreetmap.org/copyright">OSM</a>'
          url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
        />

        {depot && (
          <Marker position={[depot.lat, depot.lng]}>
            <Popup>
              <strong>Depot Location</strong><br/>
              Lat: {depot.lat}<br/>
              Lng: {depot.lng}
            </Popup>
          </Marker>
        )}

        {parcels && parcels.map((parcel, idx) => {
          let color = "#64748b";
          let fillColor = "#94a3b8";
          
          if (showZone && routes) {
            const routeIdx = routes.findIndex(route => 
              route.sequence && route.sequence.some((node: any) => node.location_id === parcel.id)
            );
            if (routeIdx !== -1) {
              color = routeColors[routeIdx % routeColors.length];
              fillColor = color;
            }
          }

          return (
            <Circle 
              key={idx} 
              center={[parcel.lat, parcel.lng]} 
              radius={80} 
              pathOptions={{ color, fillColor, fillOpacity: 0.8 }}
            >
              <Popup>
                <strong>Customer #{parcel.id}</strong><br/>
                Weight: {parcel.weight} kg<br/>
                Volume: {parcel.volume} L<br/>
                Time Window: {Math.floor(parcel.time_window[0] / 60)}:{(parcel.time_window[0] % 60).toString().padStart(2, '0')} - {Math.floor(parcel.time_window[1] / 60)}:{(parcel.time_window[1] % 60).toString().padStart(2, '0')}
              </Popup>
            </Circle>
          );
        })}

        {routes && routes.map((route, i) => {
          const color = routeColors[i % routeColors.length];
          const positions = route.sequence
            ? route.sequence.map((node: any) => [node.lat, node.lng] as [number, number])
            : [];

          const deliveryNodes = route.sequence 
            ? route.sequence.filter((node: any) => node.location_id !== 0)
            : [];
          
          const uniqueNodes: { lat: number; lng: number }[] = [];
          const seen = new Set<string>();
          for (const node of deliveryNodes) {
            const key = `${node.lat.toFixed(6)},${node.lng.toFixed(6)}`;
            if (!seen.has(key)) {
              seen.add(key);
              uniqueNodes.push({ lat: node.lat, lng: node.lng });
            }
          }
          
          const hullPoints = showZone && uniqueNodes.length >= 3 
            ? getConvexHull(uniqueNodes) 
            : [];

          return (
            <div key={i}>
              {showZone && hullPoints.length >= 3 && (
                <Polygon 
                  positions={hullPoints}
                  pathOptions={{ 
                    color, 
                    fillColor: color, 
                    fillOpacity: 0.1, 
                    dashArray: "6, 6", 
                    weight: 2 
                  }} 
                />
              )}
              {positions.length > 0 && (
                <Polyline positions={positions} pathOptions={{ color, weight: 4, opacity: 0.8 }} />
              )}
              {positions.map((pos: [number, number], j: number) => (
                <Circle key={j} center={pos} radius={50} pathOptions={{ color, fillColor: color, fillOpacity: 1 }} />
              ))}
            </div>
          );
        })}
      </MapContainer>

      <div className="absolute top-4 right-4 z-[400] bg-white p-4 rounded-lg shadow-md border border-gray-100 w-56">
        <button 
          onClick={() => setIsExpanded(!isExpanded)}
          className="flex items-center justify-between w-full font-medium text-sm text-gray-900 mb-2 focus:outline-none cursor-pointer"
        >
          <span>Active Riders ({routes ? routes.length : 0})</span>
          {isExpanded ? <ChevronUp className="w-4 h-4 text-gray-500" /> : <ChevronDown className="w-4 h-4 text-gray-500" />}
        </button>
        {isExpanded && (
          <>
            <div className="max-h-40 overflow-y-auto pr-1">
              {routes && routes.map((_, i) => (
                <div key={i} className="flex items-center gap-2 mb-2">
                  <div className="w-8 h-2 rounded-full shrink-0" style={{ backgroundColor: routeColors[i % routeColors.length] }} />
                  <span className="text-sm text-gray-600">Rider #{i + 1}</span>
                </div>
              ))}
            </div>
            <div className="mt-4 pt-4 border-t border-gray-100">
              <label className="flex items-center gap-2 text-sm text-gray-700 cursor-pointer">
                <input 
                  type="checkbox" 
                  className="rounded border-gray-300 text-blue-600 focus:ring-blue-500 cursor-pointer" 
                  checked={showZone} 
                  onChange={(e) => setShowZone(e.target.checked)} 
                />
                Show Zone
              </label>
            </div>
          </>
        )}
      </div>
    </div>
  );
}
