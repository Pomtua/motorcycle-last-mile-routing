import { Module } from '@nestjs/common';
import { AppController } from './app.controller';
import { AppService } from './app.service';
import { SimulationsModule } from './simulations/simulations.module';
import { InstancesModule } from './instances/instances.module';

@Module({
  imports: [SimulationsModule, InstancesModule],
  controllers: [AppController],
  providers: [AppService],
})
export class AppModule {}
