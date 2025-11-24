import {
  CoordinatorStorage,
  SessionRecord,
  WalletShareRecord,
  WalletShareUpsert,
} from './types';

function cloneShare(record: WalletShareRecord): WalletShareRecord {
  return {
    ...record,
    createdAt: new Date(record.createdAt),
    updatedAt: new Date(record.updatedAt),
  };
}

function cloneSession(record: SessionRecord): SessionRecord {
  return {
    ...record,
    expiresAt: new Date(record.expiresAt),
    state: record.state ? JSON.parse(JSON.stringify(record.state)) : record.state,
  };
}

export class InMemoryCoordinatorStorage implements CoordinatorStorage {
  private readonly shares = new Map<string, WalletShareRecord>();
  private readonly sessions = new Map<string, SessionRecord>();
  private readonly nonces = new Map<string, number>();

  async getWalletShare(walletId: string): Promise<WalletShareRecord | null> {
    const record = this.shares.get(walletId);
    if (!record) return null;
    return cloneShare(record);
  }

  async saveWalletShare(record: WalletShareUpsert): Promise<void> {
    const now = new Date();
    this.shares.set(record.walletId, {
      ...record,
      createdAt: now,
      updatedAt: now,
    });
  }

  async updateWalletShare(record: WalletShareUpsert): Promise<void> {
    const existing = this.shares.get(record.walletId);
    const createdAt = existing?.createdAt ?? new Date();
    this.shares.set(record.walletId, {
      ...record,
      createdAt,
      updatedAt: new Date(),
    });
  }

  private evictExpiredSession(sessionId: string): void {
    const session = this.sessions.get(sessionId);
    if (!session) return;
    if (session.expiresAt.getTime() <= Date.now()) {
      this.sessions.delete(sessionId);
    }
  }

  async getSession(sessionId: string): Promise<SessionRecord | null> {
    this.evictExpiredSession(sessionId);
    const record = this.sessions.get(sessionId);
    if (!record) return null;
    return cloneSession(record);
  }

  async saveSession(record: SessionRecord): Promise<void> {
    this.sessions.set(record.sessionId, cloneSession(record));
  }

  async deleteSession(sessionId: string): Promise<void> {
    this.sessions.delete(sessionId);
  }

  async getNonce(walletId: string): Promise<number> {
    return this.nonces.get(walletId) ?? 0;
  }

  async setNonce(walletId: string, value: number): Promise<void> {
    this.nonces.set(walletId, value);
  }
}
