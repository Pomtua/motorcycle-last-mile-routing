import { Test, TestingModule } from '@nestjs/testing';
import { INestApplication } from '@nestjs/common';
import * as request from 'supertest';
import { AppModule } from './../src/app.module';

describe('Simulations (e2e)', () => {
  let app: INestApplication;

  beforeAll(async () => {
    const moduleFixture: TestingModule = await Test.createTestingModule({
      imports: [AppModule],
    }).compile();

    app = moduleFixture.createNestApplication();
    await app.init();
  });

  it('/simulations/upload (POST) - should process CSV', () => {
    const csvContent = 'id,snapped_lat,snapped_lng,weight,volume\n1,13.75,100.5,10,20\n2,13.76,100.6,5,15';

    return request(app.getHttpServer())
      .post('/simulations/upload')
      .attach('file', Buffer.from(csvContent), 'test.csv')
      .expect(201)
      .expect((res) => {
        expect(res.body).toHaveProperty('resources');
        expect(res.body).toHaveProperty('parcels');
        expect(res.body.parcels.length).toBe(2);
        expect(res.body.parcels[0].id).toBe(1);
        expect(res.body.parcels[0].weight).toBe(10);
      });
  });

  it('/simulations (POST) - should return error for invalid engine path or execution failure', () => {
    return request(app.getHttpServer())
      .post('/simulations')
      .send({ solver: 'UNKNOWN', instanceJson: {} })
      .expect(500); // Because C++ engine will fail or json is invalid
  });

  it('/simulations (POST) - should handle OSRM server downtime gracefully', () => {
    process.env.OSRM_HOST = '192.0.2.1';
    return request(app.getHttpServer())
      .post('/simulations')
      .send({
        solver: 'GA',
        instanceJson: {
          resources: {
            depot_location: { lat: 14.0, lng: 100.0 },
            num_drivers: 2,
            max_weight_capacity: 20,
            max_volume_capacity: 50
          },
          parcels: [
            { id: 1, lat: 14.1, lng: 100.1, weight: 5, volume: 5, time_window: [480, 600] }
          ]
        }
      })
      .expect(500)
      .then(() => {
        delete process.env.OSRM_HOST;
      });
  });

  afterAll(async () => {
    await app.close();
  });
});
