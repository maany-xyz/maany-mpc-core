import { bytesToHex, utf8ToBytes } from '@noble/hashes/utils';

export function cloneBytes(source: Uint8Array): Uint8Array {
  return source.length === 0 ? new Uint8Array(0) : new Uint8Array(source);
}

export function optionalClone(source?: Uint8Array | null): Uint8Array | undefined {
  if (!source || source.length === 0) {
    return source ? new Uint8Array(0) : undefined;
  }
  return cloneBytes(source);
}

export function concatBytes(parts: readonly Uint8Array[]): Uint8Array {
  if (parts.length === 0) {
    return new Uint8Array(0);
  }
  const total = parts.reduce((sum, part) => sum + part.length, 0);
  const out = new Uint8Array(total);
  let offset = 0;
  for (const part of parts) {
    out.set(part, offset);
    offset += part.length;
  }
  return out;
}

export function fromUtf8(text: string): Uint8Array {
  return utf8ToBytes(text);
}

export function toHex(bytes: Uint8Array): string {
  return bytesToHex(bytes);
}
