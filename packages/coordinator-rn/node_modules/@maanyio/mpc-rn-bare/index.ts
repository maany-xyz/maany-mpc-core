import { NativeModules } from 'react-native';

export type Ctx = object & { readonly __brand: 'ctx' };
export type Dkg = object & { readonly __brand: 'dkg' };
export type Keypair = object & { readonly __brand: 'keypair' };
export type SignSession = object & { readonly __brand: 'sign' };

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

interface NativeBinding {
  init(): Ctx;
  shutdown(ctx: Ctx): void;
  dkgNew(ctx: Ctx, options: DkgOptions): Dkg;
  dkgStep(ctx: Ctx, dkg: Dkg, inPeerMsg?: Uint8Array): Promise<StepResult>;
  dkgFinalize(ctx: Ctx, dkg: Dkg): Keypair;
  dkgFree(dkg: Dkg): void;
  kpExport(ctx: Ctx, kp: Keypair): Uint8Array;
  kpImport(ctx: Ctx, blob: Uint8Array): Keypair;
  kpPubkey(ctx: Ctx, kp: Keypair): Pubkey;
  kpFree(kp: Keypair): void;
  signNew(ctx: Ctx, kp: Keypair, options?: SignOptions): SignSession;
  signSetMessage(ctx: Ctx, sign: SignSession, message: Uint8Array): void;
  signStep(ctx: Ctx, sign: SignSession, inPeerMsg?: Uint8Array): Promise<StepResult>;
  signFinalize(ctx: Ctx, sign: SignSession, format?: SignatureFormat): Uint8Array;
  signFree(sign: SignSession): void;
  refreshNew(ctx: Ctx, kp: Keypair, options?: { sessionId?: Uint8Array }): Dkg;
}

let cachedBinding: NativeBinding | null = null;

function ensureBinding(): NativeBinding {
  if (cachedBinding) {
    return cachedBinding;
  }

  const native = NativeModules.MaanyMpc;
  if (!native || typeof native.install !== 'function') {
    throw new Error('MaanyMpc native module not found. Have you linked the package?');
  }

  native.install();

  const globalAny = globalThis as Record<string, unknown>;
  const binding = globalAny.__maanyMpc as NativeBinding | undefined;
  if (!binding) {
    throw new Error('Failed to initialize Maany MPC JSI binding');
  }

  cachedBinding = binding;
  return binding;
}

function maybeBytes(input?: Uint8Array | null): Uint8Array | undefined {
  return input == null ? undefined : input;
}

export function init(): Ctx {
  return ensureBinding().init();
}

export function shutdown(ctx: Ctx): void {
  ensureBinding().shutdown(ctx);
}

export function dkgNew(ctx: Ctx, options: DkgOptions): Dkg {
  return ensureBinding().dkgNew(ctx, options);
}

export function dkgStep(ctx: Ctx, dkg: Dkg, inPeerMsg?: Uint8Array | null): Promise<StepResult> {
  return ensureBinding().dkgStep(ctx, dkg, maybeBytes(inPeerMsg));
}

export function dkgFinalize(ctx: Ctx, dkg: Dkg): Keypair {
  return ensureBinding().dkgFinalize(ctx, dkg);
}

export function dkgFree(dkg: Dkg): void {
  ensureBinding().dkgFree(dkg);
}

export function kpExport(ctx: Ctx, kp: Keypair): Uint8Array {
  return ensureBinding().kpExport(ctx, kp);
}

export function kpImport(ctx: Ctx, blob: Uint8Array): Keypair {
  return ensureBinding().kpImport(ctx, blob);
}

export function kpPubkey(ctx: Ctx, kp: Keypair): Pubkey {
  return ensureBinding().kpPubkey(ctx, kp);
}

export function kpFree(kp: Keypair): void {
  ensureBinding().kpFree(kp);
}

export function signNew(ctx: Ctx, kp: Keypair, options?: SignOptions): SignSession {
  return ensureBinding().signNew(ctx, kp, options);
}

export function signSetMessage(ctx: Ctx, sign: SignSession, message: Uint8Array): void {
  ensureBinding().signSetMessage(ctx, sign, message);
}

export function signStep(ctx: Ctx, sign: SignSession, inPeerMsg?: Uint8Array | null): Promise<StepResult> {
  return ensureBinding().signStep(ctx, sign, maybeBytes(inPeerMsg));
}

export function signFinalize(ctx: Ctx, sign: SignSession, format: SignatureFormat = 'der'): Uint8Array {
  return ensureBinding().signFinalize(ctx, sign, format);
}

export function signFree(sign: SignSession): void {
  ensureBinding().signFree(sign);
}

export function refreshNew(ctx: Ctx, kp: Keypair, options?: { sessionId?: Uint8Array }): Dkg {
  return ensureBinding().refreshNew(ctx, kp, options);
}
