import { Controller, Get, Post, Body, Patch, Param, Delete, UseInterceptors, UploadedFile } from '@nestjs/common';
import { SimulationsService } from './simulations.service';
import { CreateSimulationDto } from './dto/create-simulation.dto';
import { UpdateSimulationDto } from './dto/update-simulation.dto';
import { FileInterceptor } from '@nestjs/platform-express';
import { ApiTags, ApiOperation, ApiConsumes, ApiBody } from '@nestjs/swagger';

@ApiTags('Simulations')
@Controller('simulations')
export class SimulationsController {
  constructor(private readonly simulationsService: SimulationsService) {}

  @Post('upload')
  @ApiOperation({ summary: 'Upload a CSV file containing parcel data to convert to instance JSON format' })
  @ApiConsumes('multipart/form-data')
  @ApiBody({
    schema: {
      type: 'object',
      properties: {
        file: {
          type: 'string',
          format: 'binary',
        },
      },
    },
  })
  @UseInterceptors(FileInterceptor('file'))
  uploadFile(@UploadedFile() file: Express.Multer.File) {
    return this.simulationsService.processCsv(file);
  }

  @Post()
  @ApiOperation({ summary: 'Create and run a new simulation using the C++ engine' })
  create(@Body() createSimulationDto: CreateSimulationDto) {
    return this.simulationsService.create(createSimulationDto);
  }

  @Post('benchmark')
  @ApiOperation({ summary: 'Run the algorithm benchmark on all 300 instances' })
  runBenchmark() {
    return this.simulationsService.triggerBenchmark();
  }

  @Post('benchmark/stop')
  @ApiOperation({ summary: 'Interrupt and stop the active batch benchmark execution' })
  stopBenchmark() {
    return this.simulationsService.stopBenchmark();
  }

  @Get('benchmark/status')
  @ApiOperation({ summary: 'Get current on-the-fly benchmark execution status' })
  getBenchmarkStatus() {
    return this.simulationsService.getBenchmarkStatus();
  }

  @Get('benchmark/precomputed')
  @ApiOperation({ summary: 'Get instantly parsed pre-computed benchmark results' })
  getPrecomputedBenchmark() {
    return this.simulationsService.getPrecomputedResults();
  }

  @Get()
  @ApiOperation({ summary: 'Get all simulation runs' })
  findAll() {
    return this.simulationsService.findAll();
  }

  @Get(':id')
  @ApiOperation({ summary: 'Get a specific simulation run by ID' })
  findOne(@Param('id') id: string) {
    return this.simulationsService.findOne(+id);
  }

  @Patch(':id')
  @ApiOperation({ summary: 'Update a simulation run' })
  update(@Param('id') id: string, @Body() updateSimulationDto: UpdateSimulationDto) {
    return this.simulationsService.update(+id, updateSimulationDto);
  }

  @Delete(':id')
  @ApiOperation({ summary: 'Delete a simulation run' })
  remove(@Param('id') id: string) {
    return this.simulationsService.remove(+id);
  }
}

