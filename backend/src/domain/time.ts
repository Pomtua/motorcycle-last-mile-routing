const CLOCK = /^([01]?\d|2[0-3]):([0-5]\d)(?::([0-5]\d))?$/;

export function parseClock(text: string): number | null {
  const match = CLOCK.exec(text.trim());
  if (!match) {
    return null;
  }
  return Number(match[1]) * 3600 + Number(match[2]) * 60 + Number(match[3] ?? 0);
}

export function formatClock(seconds: number): string {
  const whole = Math.round(seconds);
  const hours = Math.floor(whole / 3600);
  const minutes = Math.floor((whole % 3600) / 60);
  const secs = whole % 60;
  return [hours, minutes, secs].map((part) => String(part).padStart(2, '0')).join(':');
}
