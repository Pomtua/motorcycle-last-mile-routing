import { Controller, Get, Param } from '@nestjs/common';
import { InstancesService } from './instances.service';

@Controller('instances')
export class InstancesController {
  constructor(private readonly instancesService: InstancesService) {}

  @Get()
  findAll() {
    return this.instancesService.findAll();
  }

  @Get(':type/:filename')
  findOne(@Param('type') type: string, @Param('filename') filename: string) {
    return this.instancesService.findOne(type, filename);
  }
}
