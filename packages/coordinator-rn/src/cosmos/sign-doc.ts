import { sha256 as nobleSha256 } from '@noble/hashes/sha256';
import { cloneBytes, concatBytes, fromUtf8 } from '../utils/bytes';

export interface SignDoc {
  chainId: string;
  accountNumber: string;
  sequence: string;
  bodyBytes: Uint8Array;
  authInfoBytes: Uint8Array;
}

export function makeSignBytes(doc: SignDoc): Uint8Array {
  return concatBytes([
    cloneBytes(doc.bodyBytes),
    cloneBytes(doc.authInfoBytes),
    fromUtf8(doc.chainId),
    fromUtf8(doc.accountNumber),
    fromUtf8(doc.sequence),
  ]);
}

export function sha256(data: Uint8Array): Uint8Array {
  return nobleSha256(data);
}
