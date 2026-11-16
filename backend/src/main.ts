import 'reflect-metadata';
import { Logger } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { AppModule } from './app.module';
import { config } from './config';

async function bootstrap(): Promise<void> {
  const app = await NestFactory.create(AppModule, { bodyParser: true });
  app.setGlobalPrefix('api');
  app.enableCors({ origin: config.corsOrigin === '*' ? true : config.corsOrigin.split(',') });
  app.enableShutdownHooks();
  await app.listen(config.port, '0.0.0.0');
  Logger.log(
    `API on :${config.port} — engine ${config.enginePath}, OSRM ${config.osrmUrl}, ${config.engineConcurrency} parallel solves`,
    'Bootstrap',
  );
}

void bootstrap();
