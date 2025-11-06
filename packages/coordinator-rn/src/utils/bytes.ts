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

export function toBase64(bytes: Uint8Array): string {
  if (bytes.length === 0) return '';
  let output = '';
  let i;
  for (i = 0; i + 2 < bytes.length; i += 3) {
    output += encodeTriple(bytes[i], bytes[i + 1], bytes[i + 2]);
  }
  const remaining = bytes.length - i;
  if (remaining === 1) {
    output += encodePartial(bytes[i], 0, 2);
  } else if (remaining === 2) {
    output += encodePartial(bytes[i], bytes[i + 1], 1);
  }
  return output;
}

export function fromBase64(value: string): Uint8Array {
  const clean = value.replace(/\s+/g, '');
  const len = clean.length;
  if (len === 0) return new Uint8Array(0);
  if (len % 4 !== 0) {
    throw new Error('Invalid base64 string');
  }
  const padding = clean.endsWith('==') ? 2 : clean.endsWith('=') ? 1 : 0;
  const output = new Uint8Array((len / 4) * 3 - padding);
  let outIndex = 0;
  for (let i = 0; i < len; i += 4) {
    const chunk = decodeQuad(clean.charCodeAt(i), clean.charCodeAt(i + 1), clean.charCodeAt(i + 2), clean.charCodeAt(i + 3));
    const byteCount = Math.min(3, output.length - outIndex);
    for (let j = 0; j < byteCount; j++) {
      output[outIndex + j] = chunk[j];
    }
    outIndex += byteCount;
  }
  return output;
}

const BASE64_TABLE = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

function encodeTriple(a: number, b: number, c: number): string {
  const chunk = (a << 16) | (b << 8) | c;
  return (
    BASE64_TABLE[(chunk >> 18) & 63] +
    BASE64_TABLE[(chunk >> 12) & 63] +
    BASE64_TABLE[(chunk >> 6) & 63] +
    BASE64_TABLE[chunk & 63]
  );
}

function encodePartial(a: number, b: number, padding: number): string {
  const chunk = (a << 16) | (b << 8);
  if (padding === 2) {
    return (
      BASE64_TABLE[(chunk >> 18) & 63] +
      BASE64_TABLE[(chunk >> 12) & 63] +
      '=='
    );
  }
  return (
    BASE64_TABLE[(chunk >> 18) & 63] +
    BASE64_TABLE[(chunk >> 12) & 63] +
    BASE64_TABLE[(chunk >> 6) & 63] +
    '='
  );
}

function decodeQuad(a: number, b: number, c: number, d: number): [number, number, number] {
  const sextetA = decodeChar(a);
  const sextetB = decodeChar(b);
  const sextetC = c === 61 ? 0 : decodeChar(c);
  const sextetD = d === 61 ? 0 : decodeChar(d);

  const triple = (sextetA << 18) | (sextetB << 12) | (sextetC << 6) | sextetD;
  return [
    (triple >> 16) & 0xff,
    (triple >> 8) & 0xff,
    triple & 0xff,
  ];
}

function decodeChar(charCode: number): number {
  const char = String.fromCharCode(charCode);
  const index = BASE64_TABLE.indexOf(char);
  if (index === -1) {
    throw new Error('Invalid base64 character');
  }
  return index;
}
