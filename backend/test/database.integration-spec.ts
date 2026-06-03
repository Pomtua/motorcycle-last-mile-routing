import { PrismaClient } from '@prisma/client';

describe('Database Integration & Cascading (e2e)', () => {
  let prisma: PrismaClient;

  beforeAll(async () => {
    prisma = new PrismaClient();
    await prisma.$connect();
  });

  afterAll(async () => {
    await prisma.$disconnect();
  });

  it('should preserve float precision on PostGIS coordinate retrieval', async () => {
    const highPrecisionLat = 13.075396123456;
    const highPrecisionLng = 100.597351123456;

    const run = await prisma.simulationRun.create({
      data: {
        status: 'COMPLETED',
        solver: 'GA',
        instanceJson: {},
        routes: {
          create: [
            {
              vehicleId: 0,
              totalDistance: 10.0,
              totalDuration: 10.0,
              stops: {
                create: [
                  {
                    sequence: 0,
                    parcelId: 1,
                    arrivalTime: 10.0,
                    lat: highPrecisionLat,
                    lng: highPrecisionLng,
                  }
                ]
              }
            }
          ]
        }
      },
      include: {
        routes: {
          include: {
            stops: true
          }
        }
      }
    });

    expect(run.routes[0].stops[0].lat).toBeCloseTo(highPrecisionLat, 10);
    expect(run.routes[0].stops[0].lng).toBeCloseTo(highPrecisionLng, 10);

    await prisma.simulationRun.delete({ where: { id: run.id } });
  });

  it('should clean up associated routes and stops on simulation run cascade deletion', async () => {
    const run = await prisma.simulationRun.create({
      data: {
        status: 'COMPLETED',
        solver: 'GA',
        instanceJson: {},
        routes: {
          create: [
            {
              vehicleId: 0,
              totalDistance: 10.0,
              totalDuration: 10.0,
              stops: {
                create: [
                  {
                    sequence: 0,
                    parcelId: 1,
                    arrivalTime: 10.0,
                    lat: 13.0,
                    lng: 100.0,
                  }
                ]
              }
            }
          ]
        }
      },
      include: {
        routes: {
          include: {
            stops: true
          }
        }
      }
    });

    const routeId = run.routes[0].id;
    const stopId = run.routes[0].stops[0].id;

    await prisma.simulationRun.delete({ where: { id: run.id } });

    const orphanRoute = await prisma.route.findUnique({ where: { id: routeId } });
    const orphanStop = await prisma.routeStop.findUnique({ where: { id: stopId } });

    expect(orphanRoute).toBeNull();
    expect(orphanStop).toBeNull();
  });
});
