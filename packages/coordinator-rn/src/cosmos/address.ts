import { sha256 } from '@noble/hashes/sha256';
import { ripemd160 } from '@noble/hashes/ripemd160';

const BECH32_CHARSET = 'qpzry9x8gf2tvdw0s3jn54khce6mua7l';

function polymod(values: number[]): number {
  let chk = 1;
  const GENERATORS = [0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3];
  for (const value of values) {
    const top = chk >> 25;
    chk = ((chk & 0x1ffffff) << 5) ^ value;
    for (let i = 0; i < 5; i++) {
      if (((top >> i) & 1) === 1) {
        chk ^= GENERATORS[i];
      }
    }
  }
  return chk;
}

function hrpExpand(hrp: string): number[] {
  const result: number[] = [];
  for (const char of hrp) {
    result.push(char.charCodeAt(0) >> 5);
  }
  result.push(0);
  for (const char of hrp) {
    result.push(char.charCodeAt(0) & 31);
  }
  return result;
}

function createChecksum(hrp: string, data: number[]): number[] {
  const values = hrpExpand(hrp).concat(data).concat([0, 0, 0, 0, 0, 0]);
  const mod = polymod(values) ^ 1;
  const result: number[] = [];
  for (let p = 0; p < 6; p++) {
    result.push((mod >> (5 * (5 - p))) & 31);
  }
  return result;
}

function convertBits(data: Uint8Array, from: number, to: number, pad = true): number[] {
  let acc = 0;
  let bits = 0;
  const result: number[] = [];
  const maxv = (1 << to) - 1;
  for (const value of data) {
    acc = (acc << from) | value;
    bits += from;
    while (bits >= to) {
      bits -= to;
      result.push((acc >> bits) & maxv);
    }
  }
  if (pad) {
    if (bits > 0) result.push((acc << (to - bits)) & maxv);
  } else if (bits >= from || ((acc << (to - bits)) & maxv)) {
    throw new Error('Invalid incomplete group');
  }
  return result;
}

export function pubkeyToCosmosAddress(pubkeyCompressed: Uint8Array, prefix = 'cosmos'): string {
  const sha = sha256(pubkeyCompressed);
  const ripemd = ripemd160(sha);
  const words = convertBits(ripemd, 8, 5, true);
  const checksum = createChecksum(prefix, words);
  const combined = words.concat(checksum);
  let address = prefix + '1';
  for (const value of combined) {
    address += BECH32_CHARSET[value];
  }
  return address;
}
