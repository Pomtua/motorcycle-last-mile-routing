import { BadGatewayException, BadRequestException, Injectable } from '@nestjs/common';
import { config } from '../config';

export type LngLat = [number, number];

export interface Matrix {
  size: number;
  durations: Float64Array;
  distances: Float64Array;
}

export interface Waypoint {
  location: LngLat;
  distance: number;
}

export interface TableResult extends Matrix {
  waypoints: Waypoint[];
}

const MAX_ROUTE_WAYPOINTS = 400;

@Injectable()
export class OsrmService {
  async table(points: LngLat[], labels: string[]): Promise<TableResult> {
    const body = await this.get<{
      durations?: (number | null)[][];
      distances?: (number | null)[][];
      sources?: { location?: LngLat; distance?: number }[];
    }>(`table/v1/${config.osrmProfile}/${this.coordinates(points)}?annotations=duration,distance`);
    const size = points.length;
    const durations = this.flatten(body.durations, size, 'durations', labels);
    const distances = this.flatten(body.distances, size, 'distances', labels);
    return { size, durations, distances, waypoints: this.waypoints(body.sources, size) };
  }

  async route(points: LngLat[]): Promise<LngLat[]> {
    const path: LngLat[] = [];
    for (let start = 0; start < points.length - 1; start += MAX_ROUTE_WAYPOINTS - 1) {
      const segment = points.slice(start, start + MAX_ROUTE_WAYPOINTS);
      const body = await this.get<{ routes?: { geometry: { coordinates: LngLat[] } }[] }>(
        `route/v1/${config.osrmProfile}/${this.coordinates(segment)}?overview=full&geometries=geojson&steps=false`,
      );
      const coordinates = body.routes?.[0]?.geometry.coordinates;
      if (!coordinates || coordinates.length === 0) {
        throw new BadGatewayException('OSRM returned no route geometry');
      }
      path.push(...(path.length > 0 ? coordinates.slice(1) : coordinates));
    }
    return path;
  }

  async ping(): Promise<boolean> {
    try {
      await this.get('nearest/v1/' + config.osrmProfile + '/100.5018,13.7563');
      return true;
    } catch {
      return false;
    }
  }

  private coordinates(points: LngLat[]): string {
    return points.map(([lng, lat]) => `${lng.toFixed(6)},${lat.toFixed(6)}`).join(';');
  }

  private waypoints(sources: { location?: LngLat; distance?: number }[] | undefined, size: number): Waypoint[] {
    if (!sources || sources.length !== size) {
      throw new BadGatewayException('OSRM table returned the wrong number of snapped points');
    }
    return sources.map((source, index) => {
      const location = source.location;
      const distance = source.distance;
      if (!location || location.length !== 2 || !location.every(Number.isFinite) || !Number.isFinite(distance)) {
        throw new BadGatewayException(`OSRM table returned no snapped location for point ${index}`);
      }
      return { location: [location[0], location[1]], distance: distance as number };
    });
  }

  private flatten(rows: (number | null)[][] | undefined, size: number, name: string, labels: string[]): Float64Array {
    if (!rows || rows.length !== size) {
      throw new BadGatewayException(`OSRM ${name} matrix has the wrong size`);
    }
    const flat = new Float64Array(size * size);
    for (let i = 0; i < size; i += 1) {
      const row = rows[i];
      if (!row || row.length !== size) {
        throw new BadGatewayException(`OSRM ${name} row ${i} has the wrong size`);
      }
      for (let j = 0; j < size; j += 1) {
        const value = row[j];
        if (value === null || !Number.isFinite(value) || value < 0) {
          throw new BadRequestException(
            `No road connection between ${labels[i]} and ${labels[j]}; move the point onto a reachable road`,
          );
        }
        flat[i * size + j] = i === j ? 0 : value;
      }
    }
    return flat;
  }

  private async get<T>(path: string): Promise<T> {
    let response: Response;
    try {
      response = await fetch(`${config.osrmUrl}/${path}`, { signal: AbortSignal.timeout(config.osrmTimeoutMs) });
    } catch (error) {
      throw new BadGatewayException(`OSRM is not reachable at ${config.osrmUrl}: ${(error as Error).message}`);
    }
    const body = (await response.json().catch(() => ({}))) as { code?: string; message?: string };
    if (!response.ok || body.code !== 'Ok') {
      throw new BadGatewayException(`OSRM error: ${body.message ?? body.code ?? response.status}`);
    }
    return body as T;
  }
}
