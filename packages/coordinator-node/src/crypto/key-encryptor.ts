import { createCipheriv, createDecipheriv, randomBytes } from 'node:crypto';

export interface KeyEncryptor {
  encryptShare(raw: Uint8Array): Promise<string>;
  decryptShare(encoded: string): Promise<Uint8Array>;
}

function normalizeKeyBytes(key: string | Buffer): Buffer {
  if (Buffer.isBuffer(key)) {
    if (key.length !== 32) {
      throw new Error(`expected 32-byte master key, received ${key.length}`);
    }
    return Buffer.from(key);
  }
  const trimmed = key.trim();
  const buf = Buffer.from(trimmed, /^[0-9a-fA-F]+$/.test(trimmed) ? 'hex' : 'base64');
  if (buf.length !== 32) {
    throw new Error(`expected 32-byte master key, received ${buf.length}`);
  }
  return buf;
}

export class AesGcmKeyEncryptor implements KeyEncryptor {
  private readonly masterKey: Buffer;

  constructor(masterKey: string | Buffer) {
    this.masterKey = normalizeKeyBytes(masterKey);
  }

  async encryptShare(raw: Uint8Array): Promise<string> {
    const iv = randomBytes(12);
    const cipher = createCipheriv('aes-256-gcm', this.masterKey, iv);
    const ciphertext = Buffer.concat([cipher.update(Buffer.from(raw)), cipher.final()]);
    const tag = cipher.getAuthTag();
    return Buffer.concat([iv, tag, ciphertext]).toString('base64');
  }

  async decryptShare(encoded: string): Promise<Uint8Array> {
    const buf = Buffer.from(encoded, 'base64');
    const iv = buf.subarray(0, 12);
    const tag = buf.subarray(12, 28);
    const ciphertext = buf.subarray(28);
    const decipher = createDecipheriv('aes-256-gcm', this.masterKey, iv);
    decipher.setAuthTag(tag);
    const plaintext = Buffer.concat([decipher.update(ciphertext), decipher.final()]);
    return new Uint8Array(plaintext);
  }
}

export class PlaintextKeyEncryptor implements KeyEncryptor {
  async encryptShare(raw: Uint8Array): Promise<string> {
    return Buffer.from(raw).toString('base64');
  }

  async decryptShare(encoded: string): Promise<Uint8Array> {
    return new Uint8Array(Buffer.from(encoded, 'base64'));
  }
}

export function createEnvKeyEncryptor(envVar = 'MAANY_COORDINATOR_MASTER_KEY'): KeyEncryptor {
  const value = process.env[envVar];
  if (!value) {
    throw new Error(`Missing ${envVar} for KeyEncryptor`);
  }
  return new AesGcmKeyEncryptor(value);
}
