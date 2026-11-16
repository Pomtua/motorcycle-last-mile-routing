import {
  BadRequestException,
  Body,
  Controller,
  Delete,
  Get,
  Header,
  HttpCode,
  Param,
  ParseUUIDPipe,
  Post,
  UploadedFile,
  UseInterceptors,
} from '@nestjs/common';
import { FileInterceptor } from '@nestjs/platform-express';
import { config } from '../config';
import { CSV_TEMPLATE, ParcelError } from '../domain/parcels';
import { parseMaxSnapDistance } from '../domain/snapping';
import { DatasetsService } from './datasets.service';

const upload = FileInterceptor('file', { limits: { fileSize: config.maxUploadBytes } });

function fileText(file: Express.Multer.File | undefined): string {
  if (!file) {
    throw new BadRequestException('file is required');
  }
  return file.buffer.toString('utf8');
}

function datasetName(name: string | undefined, file: Express.Multer.File | undefined): string {
  const fallback = file?.originalname.replace(/\.[^.]+$/, '') ?? '';
  return (name ?? '').trim() || fallback;
}

@Controller('datasets')
export class DatasetsController {
  constructor(private readonly datasets: DatasetsService) {}

  @Get('template.csv')
  @Header('Content-Type', 'text/csv; charset=utf-8')
  @Header('Content-Disposition', 'attachment; filename="parcels-template.csv"')
  template(): string {
    return CSV_TEMPLATE + '\n';
  }

  @Post()
  @UseInterceptors(upload)
  createFromCsv(
    @UploadedFile() file: Express.Multer.File | undefined,
    @Body('name') name?: string,
    @Body('maxSnapDistanceM') maxSnapDistanceM?: string,
  ) {
    let maxSnapDistance: number;
    try {
      maxSnapDistance = parseMaxSnapDistance(maxSnapDistanceM);
    } catch (error) {
      if (error instanceof ParcelError) {
        throw new BadRequestException({ message: error.message, errors: error.errors });
      }
      throw error;
    }
    return this.datasets.createFromCsv(datasetName(name, file), fileText(file), maxSnapDistance);
  }

  @Post('instance')
  @UseInterceptors(upload)
  createFromInstance(@UploadedFile() file: Express.Multer.File | undefined, @Body('name') name?: string) {
    return this.datasets.createFromInstance(datasetName(name, file), fileText(file));
  }

  @Get()
  list() {
    return this.datasets.list();
  }

  @Get(':id')
  detail(@Param('id', ParseUUIDPipe) id: string) {
    return this.datasets.detail(id);
  }

  @Delete(':id')
  @HttpCode(204)
  async remove(@Param('id', ParseUUIDPipe) id: string): Promise<void> {
    await this.datasets.remove(id);
  }
}
