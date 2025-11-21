1. Split what you need to store

For a 2-of-2 Maany wallet you roughly have:

A. Short-lived (ephemeral) state

Active DKG sessions

In-progress signing sessions

Nonce counters / replay protection

Temporary commitments / messages between rounds

Properties:

Needs low latency, but not necessarily long-term durability

Can be safely lost on crash if protocol is restartable / resumable

👉 Ideal: Redis (or even in-memory as a fallback for dev).

B. Long-lived (persistent) state

Server key share for each wallet

Wallet metadata: user/app IDs, device IDs, public key, creation time

Counters / sequence numbers you don’t want to roll back (e.g. signing counters if protocol requires strict monotonicity)

Audit logs (optional but nice)

Properties:

Must be durable

Must be encrypted at rest

Must support migrations & future extensions

👉 Ideal: Postgres (or any SQL DB) as the baseline.
Later / in prod hard mode: wrap Postgres with KMS or Vault so the DB never stores plaintext shares.

2. Suggested baseline stack

To keep things concrete:

Database: Postgres

ORM: Prisma or TypeORM (you’re already heavily TypeScript-y, Prisma usually feels nice)

Ephemeral store: Redis

Crypto: Node’s crypto or libsodium-wrappers for encrypting shares before they ever hit the DB

Master key:

Phase 1: environment var (strong random, rotated rarely, dev / staging)

Phase 2: Cloud KMS (AWS KMS / GCP KMS / Azure Key Vault) or HashiCorp Vault transit engine

3. Define a clean storage interface in coordinator-node

In @maany/mpc-coordinator-node you don’t want to depend on “Postgres” directly. Instead define an interface:

// packages/coordinator-node/src/storage/types.ts
export interface WalletShareRecord {
  walletId: string;
  appId: string;
  userId: string;
  deviceId: string;
  publicKey: string;        // serialized aggregate pubkey
  encryptedServerShare: string; // base64/hex of encrypted blob
  createdAt: Date;
  updatedAt: Date;
}

export interface SessionRecord {
  sessionId: string;
  walletId: string;
  type: 'DKG' | 'SIGN';
  state: unknown;        // or a typed state interface
  expiresAt: Date;
}

export interface CoordinatorStorage {
  // Persistent
  getWalletShare(walletId: string): Promise<WalletShareRecord | null>;
  saveWalletShare(record: WalletShareRecord): Promise<void>;
  updateWalletShare(record: WalletShareRecord): Promise<void>;

  // Ephemeral sessions
  getSession(sessionId: string): Promise<SessionRecord | null>;
  saveSession(record: SessionRecord): Promise<void>;
  deleteSession(sessionId: string): Promise<void>;

  // Optional: monotonic counters
  getNonce(walletId: string): Promise<number>;
  setNonce(walletId: string, value: number): Promise<void>;
}


Then expose a way to inject an implementation when instantiating the coordinator:

// packages/coordinator-node/src/Coordinator.ts
export interface CoordinatorOptions {
  storage: CoordinatorStorage;
  // ... other options (transports, logging, etc.)
}

export class Coordinator {
  private storage: CoordinatorStorage;

  constructor(opts: CoordinatorOptions) {
    this.storage = opts.storage;
  }

  // inside your DKG completion:
  private async persistServerShare(ctx: {
    walletId: string;
    appId: string;
    userId: string;
    deviceId: string;
    publicKey: string;
    rawServerShare: Uint8Array;
  }) {
    const encryptedServerShare = await this.encryptShare(ctx.rawServerShare);
    await this.storage.saveWalletShare({
      walletId: ctx.walletId,
      appId: ctx.appId,
      userId: ctx.userId,
      deviceId: ctx.deviceId,
      publicKey: ctx.publicKey,
      encryptedServerShare,
      createdAt: new Date(),
      updatedAt: new Date(),
    });
  }
}


Now coordinator-node doesn’t care how you store; the tiny Node “server” app wires in the concrete adapter.

4. Concrete implementation: Postgres + Redis
4.1. Prisma schema example
model WalletShare {
  walletId           String   @id
  appId              String
  userId             String
  deviceId           String
  publicKey          String
  encryptedServerShare Bytes
  createdAt          DateTime @default(now())
  updatedAt          DateTime @updatedAt

  @@index([appId, userId])
}

model WalletNonce {
  walletId String @id
  value    Int
}

model Session {
  sessionId String   @id
  walletId  String
  type      String
  state     Bytes
  expiresAt DateTime

  @@index([walletId])
  @@index([expiresAt])
}

4.2. Storage adapter skeleton
// coordinator-node-storage-postgres/src/PostgresStorage.ts
import { PrismaClient } from '@prisma/client';
import { CoordinatorStorage, WalletShareRecord, SessionRecord } from '@maany/mpc-coordinator-node';

export class PostgresRedisStorage implements CoordinatorStorage {
  constructor(
    private prisma: PrismaClient,
    private redis: { get(key: string): Promise<string | null>; set(key: string, value: string, mode?: string, duration?: number): Promise<void>; del(key: string): Promise<void>; },
  ) {}

  async getWalletShare(walletId: string): Promise<WalletShareRecord | null> {
    const row = await this.prisma.walletShare.findUnique({ where: { walletId } });
    if (!row) return null;
    return {
      walletId: row.walletId,
      appId: row.appId,
      userId: row.userId,
      deviceId: row.deviceId,
      publicKey: row.publicKey,
      encryptedServerShare: Buffer.from(row.encryptedServerShare).toString('base64'),
      createdAt: row.createdAt,
      updatedAt: row.updatedAt,
    };
  }

  async saveWalletShare(record: WalletShareRecord): Promise<void> {
    await this.prisma.walletShare.create({
      data: {
        walletId: record.walletId,
        appId: record.appId,
        userId: record.userId,
        deviceId: record.deviceId,
        publicKey: record.publicKey,
        encryptedServerShare: Buffer.from(record.encryptedServerShare, 'base64'),
      },
    });
  }

  async updateWalletShare(record: WalletShareRecord): Promise<void> {
    await this.prisma.walletShare.update({
      where: { walletId: record.walletId },
      data: {
        appId: record.appId,
        userId: record.userId,
        deviceId: record.deviceId,
        publicKey: record.publicKey,
        encryptedServerShare: Buffer.from(record.encryptedServerShare, 'base64'),
      },
    });
  }

  // == Sessions in Redis ==
  async getSession(sessionId: string): Promise<SessionRecord | null> {
    const raw = await this.redis.get(`session:${sessionId}`);
    return raw ? JSON.parse(raw) as SessionRecord : null;
  }

  async saveSession(record: SessionRecord): Promise<void> {
    const ttlSeconds = Math.max(60, Math.floor((record.expiresAt.getTime() - Date.now()) / 1000));
    await this.redis.set(`session:${record.sessionId}`, JSON.stringify(record), 'EX', ttlSeconds);
  }

  async deleteSession(sessionId: string): Promise<void> {
    await this.redis.del(`session:${sessionId}`);
  }

  async getNonce(walletId: string): Promise<number> {
    const row = await this.prisma.walletNonce.findUnique({ where: { walletId } });
    return row ? row.value : 0;
  }

  async setNonce(walletId: string, value: number): Promise<void> {
    await this.prisma.walletNonce.upsert({
      where: { walletId },
      create: { walletId, value },
      update: { value },
    });
  }
}


Your tiny Node app then does:

const prisma = new PrismaClient();
const redis = new Redis(process.env.REDIS_URL!);

const storage = new PostgresRedisStorage(prisma, redis);
const coordinator = new Coordinator({ storage, /* ... */ });

5. Encrypting the server share properly

You never want raw server share bytes in DB or logs.

5.1. Simple (Phase 1): local master key
import crypto from 'crypto';

const MASTER_KEY = Buffer.from(process.env.MAANY_COORDINATOR_MASTER_KEY!, 'hex'); // 32 bytes

function encryptShare(raw: Uint8Array): string {
  const iv = crypto.randomBytes(12);
  const cipher = crypto.createCipheriv('aes-256-gcm', MASTER_KEY, iv);
  const ciphertext = Buffer.concat([cipher.update(raw), cipher.final()]);
  const tag = cipher.getAuthTag();
  return Buffer.concat([iv, tag, ciphertext]).toString('base64');
}

function decryptShare(encoded: string): Uint8Array {
  const buf = Buffer.from(encoded, 'base64');
  const iv = buf.subarray(0, 12);
  const tag = buf.subarray(12, 28);
  const ciphertext = buf.subarray(28);
  const decipher = crypto.createDecipheriv('aes-256-gcm', MASTER_KEY, iv);
  decipher.setAuthTag(tag);
  const plaintext = Buffer.concat([decipher.update(ciphertext), decipher.final()]);
  return new Uint8Array(plaintext);
}


You can hide that behind a KeyEncryptor interface so later you swap in KMS without touching the storage adapter.

5.2. Strong (Phase 2+): KMS / Vault

Master key lives in AWS KMS / GCP KMS / Vault transit.

Coordinator sends the share to KMS for encryption/decryption.

Postgres only ever sees ciphertext.

This gives you:

Hardware-level key protection

Rotation / access control managed by the cloud / Vault

Easier audits

6. Why this fits Maany nicely

You keep @maany/mpc-coordinator-node clean and reusable (anyone can implement their own storage adapter: “memory only”, “Dynamo only”, “Vault only”…).

Your tiny Node coordinator service just wires in a PostgresRedisStorage + KeyEncryptor implementation.

You can start with a simple local/dev config (Docker Postgres + Redis, env master key), and later, for Maany production, swap in:

Managed Postgres (RDS / Cloud SQL)

Managed Redis (Elasticache / Memorystore)

KMS/Vault-backed encryption

Optional: multi-region replication if you go global