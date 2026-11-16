import { Controller, Get, Param, ParseUUIDPipe, Res } from '@nestjs/common';
import type { Response } from 'express';
import { JobsService } from './jobs.service';

@Controller('jobs')
export class JobsController {
  constructor(private readonly jobs: JobsService) {}

  @Get(':id/result')
  result(@Param('id', ParseUUIDPipe) id: string) {
    return this.jobs.result(id);
  }

  @Get(':id/export.csv')
  async export(@Param('id', ParseUUIDPipe) id: string, @Res() response: Response): Promise<void> {
    const { filename, body } = await this.jobs.exportCsv(id);
    response.setHeader('Content-Type', 'text/csv; charset=utf-8');
    response.setHeader('Content-Disposition', `attachment; filename="${filename}"`);
    response.send(body);
  }
}
