import type { IncomingMessage, ServerResponse } from 'node:http';

export class Invalid extends Error {
  field?: string;
  constructor(message: string, field?: string) {
    super(message);
    this.field = field;
  }
}

export function send(res: ServerResponse, status: number, body?: unknown, headers: Record<string, string> = {}) {
  res.statusCode = status;
  for (const [k, v] of Object.entries(headers)) res.setHeader(k, v);
  if (body === undefined) return res.end();
  if (body instanceof Uint8Array) {
    res.setHeader('Content-Type', 'application/octet-stream');
    return res.end(body);
  }
  res.setHeader('Content-Type', 'application/json');
  res.end(JSON.stringify(body));
}

export function readText(req: IncomingMessage): Promise<string> {
  return new Promise((resolve, reject) => {
    let data = '';
    req.setEncoding('utf8');
    req.on('data', (c: string) => (data += c));
    req.on('end', () => resolve(data));
    req.on('error', reject);
  });
}

export async function readJson(req: IncomingMessage): Promise<unknown> {
  const text = await readText(req);
  try {
    return JSON.parse(text);
  } catch {
    throw new Invalid('Invalid JSON');
  }
}
