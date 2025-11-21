export type Ctx = { readonly __brand: 'ctx' };
export type Dkg = { readonly __brand: 'dkg' };
export type Keypair = { readonly __brand: 'keypair' };
export type SignSession = { readonly __brand: 'sign' };

export interface DkgOptions {
  role: 'device' | 'server';
  keyId?: Uint8Array;
  sessionId?: Uint8Array;
}

export interface StepResult {
  outMsg?: Uint8Array;
  done: boolean;
}

export interface SignOptions {
  sessionId?: Uint8Array;
  extraAad?: Uint8Array;
}

export type SignatureFormat = 'der' | 'raw-rs';

export interface Pubkey {
  curve: number;
  compressed: Uint8Array;
}

export interface BackupCiphertext {
  kind: 'device' | 'server';
  curve: 'secp256k1' | 'ed25519';
  scheme: 'ecdsa-2p' | 'ecdsa-tn' | 'schnorr-2p';
  keyId: Uint8Array;
  threshold: number;
  shareCount: number;
  label: Uint8Array;
  blob: Uint8Array;
}

export interface BackupCreateOptions {
  threshold?: number;
  shareCount?: number;
  label?: Uint8Array;
}

export interface BackupCreateResult {
  ciphertext: BackupCiphertext;
  shares: Uint8Array[];
}

export declare function init(): Ctx;
export declare function shutdown(ctx: Ctx): void;
export declare function dkgNew(ctx: Ctx, options: DkgOptions): Dkg;
export declare function dkgStep(ctx: Ctx, dkg: Dkg, inPeerMsg?: Uint8Array | null): Promise<StepResult>;
export declare function dkgFinalize(ctx: Ctx, dkg: Dkg): Keypair;
export declare function dkgFree(dkg: Dkg): void;
export declare function kpExport(ctx: Ctx, kp: Keypair): Uint8Array;
export declare function kpImport(ctx: Ctx, blob: Uint8Array): Keypair;
export declare function kpPubkey(ctx: Ctx, kp: Keypair): Pubkey;
export declare function kpFree(kp: Keypair): void;
export declare function signNew(ctx: Ctx, kp: Keypair, options?: SignOptions): SignSession;
export declare function signSetMessage(ctx: Ctx, sign: SignSession, message: Uint8Array): void;
export declare function signStep(ctx: Ctx, sign: SignSession, inPeerMsg?: Uint8Array | null): Promise<StepResult>;
export declare function signFinalize(ctx: Ctx, sign: SignSession, format?: SignatureFormat): Uint8Array;
export declare function signFree(sign: SignSession): void;
export declare function refreshNew(ctx: Ctx, kp: Keypair, options?: { sessionId?: Uint8Array }): Dkg;
export declare function backupCreate(ctx: Ctx, kp: Keypair, options?: BackupCreateOptions): BackupCreateResult;
export declare function backupRestore(ctx: Ctx, ciphertext: BackupCiphertext, shares: Uint8Array[]): Keypair;
