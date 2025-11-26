import type { PrismaClient } from '@prisma/client';
import type { Redis } from 'ioredis';
import {
  CoordinatorStorage,
  SessionRecord,
  WalletShareRecord,
  WalletShareUpsert,
  WalletBackupRecord,
  WalletBackupUpsert,
} from './types';

const SESSION_PREFIX = 'session:';

function serializeShare(record: WalletShareUpsert): {
  walletId: string;
  appId?: string | null;
  userId?: string | null;
  deviceId?: string | null;
  publicKey?: string | null;
  encryptedServerShare: Buffer;
} {
  return {
    walletId: record.walletId,
    appId: record.appId ?? null,
    userId: record.userId ?? null,
    deviceId: record.deviceId ?? null,
    publicKey: record.publicKey ?? null,
    encryptedServerShare: Buffer.from(record.encryptedServerShare, 'base64'),
  };
}

function deserializeShare(row: {
  walletId: string;
  appId: string | null;
  userId: string | null;
  deviceId: string | null;
  publicKey: string | null;
  encryptedServerShare: Buffer;
  createdAt: Date;
  updatedAt: Date;
}): WalletShareRecord {
  return {
    walletId: row.walletId,
    appId: row.appId ?? undefined,
    userId: row.userId ?? undefined,
    deviceId: row.deviceId ?? undefined,
    publicKey: row.publicKey ?? undefined,
    encryptedServerShare: row.encryptedServerShare.toString('base64'),
    createdAt: row.createdAt,
    updatedAt: row.updatedAt,
  };
}

function serializeBackup(record: WalletBackupUpsert) {
  return {
    walletId: record.walletId,
    ciphertextKind: record.ciphertextKind,
    ciphertextCurve: record.ciphertextCurve,
    ciphertextScheme: record.ciphertextScheme,
    ciphertextKeyId: record.ciphertextKeyId,
    ciphertextThreshold: record.ciphertextThreshold,
    ciphertextShareCount: record.ciphertextShareCount,
    ciphertextLabel: Buffer.from(record.ciphertextLabel, 'base64'),
    ciphertextBlob: Buffer.from(record.ciphertextBlob, 'base64'),
    coordinatorFragment: Buffer.from(record.encryptedCoordinatorFragment, 'base64'),
  };
}

function deserializeBackup(row: {
  walletId: string;
  ciphertextKind: string;
  ciphertextCurve: string;
  ciphertextScheme: string;
  ciphertextKeyId: string;
  ciphertextThreshold: number;
  ciphertextShareCount: number;
  ciphertextLabel: Buffer;
  ciphertextBlob: Buffer;
  coordinatorFragment: Buffer;
  createdAt: Date;
  updatedAt: Date;
}): WalletBackupRecord {
  return {
    walletId: row.walletId,
    ciphertextKind: row.ciphertextKind,
    ciphertextCurve: row.ciphertextCurve,
    ciphertextScheme: row.ciphertextScheme,
    ciphertextKeyId: row.ciphertextKeyId,
    ciphertextThreshold: row.ciphertextThreshold,
    ciphertextShareCount: row.ciphertextShareCount,
    ciphertextLabel: row.ciphertextLabel.toString('base64'),
    ciphertextBlob: row.ciphertextBlob.toString('base64'),
    encryptedCoordinatorFragment: row.coordinatorFragment.toString('base64'),
    createdAt: row.createdAt,
    updatedAt: row.updatedAt,
  };
}

interface SerializedSession {
  sessionId: string;
  walletId?: string;
  type: SessionRecord['type'];
  state: unknown;
  expiresAt: string;
}

function serializeSession(record: SessionRecord): SerializedSession {
  return {
    sessionId: record.sessionId,
    walletId: record.walletId,
    type: record.type,
    state: record.state,
    expiresAt: record.expiresAt.toISOString(),
  };
}

function deserializeSession(raw: string | null): SessionRecord | null {
  if (!raw) return null;
  try {
    const data = JSON.parse(raw) as SerializedSession;
    const expiresAt = new Date(data.expiresAt);
    if (Number.isNaN(expiresAt.getTime())) return null;
    if (expiresAt.getTime() <= Date.now()) return null;
    return {
      sessionId: data.sessionId,
      walletId: data.walletId,
      type: data.type,
      state: data.state,
      expiresAt,
    };
  } catch {
    return null;
  }
}

export class PostgresRedisStorage implements CoordinatorStorage {
  constructor(private readonly prisma: PrismaClient, private readonly redis: Redis) {}

  async getWalletShare(walletId: string): Promise<WalletShareRecord | null> {
    const row = await this.prisma.walletShare.findUnique({ where: { walletId } });
    if (!row) return null;
    return deserializeShare(row);
  }

  async saveWalletShare(record: WalletShareUpsert): Promise<void> {
    await this.prisma.walletShare.create({ data: serializeShare(record) });
  }

  async updateWalletShare(record: WalletShareUpsert): Promise<void> {
    await this.prisma.walletShare.update({ where: { walletId: record.walletId }, data: serializeShare(record) });
  }

  async getSession(sessionId: string): Promise<SessionRecord | null> {
    const raw = await this.redis.get(`${SESSION_PREFIX}${sessionId}`);
    return deserializeSession(raw);
  }

  async saveSession(record: SessionRecord): Promise<void> {
    const ttlSeconds = Math.max(60, Math.floor((record.expiresAt.getTime() - Date.now()) / 1000));
    await this.redis.set(
      `${SESSION_PREFIX}${record.sessionId}`,
      JSON.stringify(serializeSession(record)),
      'EX',
      ttlSeconds
    );
  }

  async deleteSession(sessionId: string): Promise<void> {
    await this.redis.del(`${SESSION_PREFIX}${sessionId}`);
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

  async getWalletBackup(walletId: string): Promise<WalletBackupRecord | null> {
    const row = await this.prisma.walletBackup.findUnique({ where: { walletId } });
    if (!row) return null;
    return deserializeBackup(row);
  }

  async upsertWalletBackup(record: WalletBackupUpsert): Promise<void> {
    const data = serializeBackup(record);
    await this.prisma.walletBackup.upsert({
      where: { walletId: record.walletId },
      create: data,
      update: data,
    });
  }
}
