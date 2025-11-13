import { createHash } from 'node:crypto';

export interface SignDoc {
  chainId: string;
  accountNumber: string;
  sequence: string;
  bodyBytes: Uint8Array;
  authInfoBytes: Uint8Array;
}

export function makeSignBytes(doc: SignDoc): Uint8Array {
  return Buffer.concat([
    Buffer.from(doc.bodyBytes),
    Buffer.from(doc.authInfoBytes),
    Buffer.from(doc.chainId, 'utf8'),
    Buffer.from(doc.accountNumber),
    Buffer.from(doc.sequence),
  ]);
}

export function sha256(data: Uint8Array): Uint8Array {
  return createHash('sha256').update(data).digest();
}
