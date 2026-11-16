import { Body, Controller, Get, HttpCode, MessageEvent, Param, ParseUUIDPipe, Post, Query, Sse } from '@nestjs/common';
import { filter, interval, map, merge, Observable } from 'rxjs';
import { JobRunner } from './job-runner.service';
import { RunsService } from './runs.service';

@Controller('runs')
export class RunsController {
  constructor(
    private readonly runs: RunsService,
    private readonly runner: JobRunner,
  ) {}

  @Post()
  @HttpCode(202)
  create(@Body() body: unknown) {
    return this.runs.create(body);
  }

  @Get()
  list(@Query('datasetId', ParseUUIDPipe) datasetId: string) {
    return this.runs.list(datasetId);
  }

  @Get(':id')
  detail(@Param('id', ParseUUIDPipe) id: string) {
    return this.runs.detail(id);
  }

  @Sse(':id/events')
  events(@Param('id', ParseUUIDPipe) id: string): Observable<MessageEvent> {
    const changes = this.runner.events.pipe(
      filter((event) => event.runId === id),
      map((event): MessageEvent => ({ type: 'job', data: event })),
    );
    const heartbeat = interval(15_000).pipe(map((): MessageEvent => ({ type: 'ping', data: {} })));
    return merge(changes, heartbeat);
  }

  @Post(':id/cancel')
  cancel(@Param('id', ParseUUIDPipe) id: string) {
    return this.runs.cancel(id);
  }
}
