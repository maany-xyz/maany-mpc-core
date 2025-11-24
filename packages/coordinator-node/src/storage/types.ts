export interface WalletShareRecord {
  walletId: string;
  appId?: string;
  userId?: string;
  deviceId?: string;
  publicKey?: string;
  encryptedServerShare: string; // base64 encoded ciphertext
  createdAt: Date;
  updatedAt: Date;
}

export type WalletShareUpsert = Omit<WalletShareRecord, 'createdAt' | 'updatedAt'>;

export interface SessionRecord {
  sessionId: string;
  walletId?: string;
  type: 'DKG' | 'SIGN' | 'REFRESH';
  state: unknown;
  expiresAt: Date;
}

export interface CoordinatorStorage {
  getWalletShare(walletId: string): Promise<WalletShareRecord | null>;
  saveWalletShare(record: WalletShareUpsert): Promise<void>;
  updateWalletShare(record: WalletShareUpsert): Promise<void>;

  getSession(sessionId: string): Promise<SessionRecord | null>;
  saveSession(record: SessionRecord): Promise<void>;
  deleteSession(sessionId: string): Promise<void>;

  getNonce(walletId: string): Promise<number>;
  setNonce(walletId: string, value: number): Promise<void>;
}
