import type { Transport, Participant } from '../transport';
import type { ShareStorage } from '../storage';
import * as mpc from '@maanyio/mpc-rn-bare';
import { cloneBytes, optionalClone, toHex } from '../utils/bytes';

type BackupCapableBinding = typeof mpc & {
  backupCreate(ctx: mpc.Ctx, kp: mpc.Keypair, options?: BackupCreateOptions): BackupCreateResult;
};

const bindingWithBackup = mpc as BackupCapableBinding;

export interface DkgResult {
  deviceKeypair: mpc.Keypair;
  serverKeypair: mpc.Keypair | null;
  backup?: DeviceBackupArtifacts | null;
}

export interface DkgOptions {
  transport: Transport;
  storage: ShareStorage;
  keyId?: Uint8Array;
  sessionId?: Uint8Array;
  mode?: 'dual' | 'device-only';
  backup?: DeviceBackupOptions;
}

export interface DeviceBackupOptions {
  enabled?: boolean;
  threshold?: number;
  shareCount?: number;
  label?: Uint8Array;
}

export interface DeviceBackupArtifacts {
  ciphertext: BackupCiphertext;
  shares: Uint8Array[];
}

export type BackupCiphertextKind = 'device' | 'server';
export type BackupCurve = 'secp256k1' | 'ed25519';
export type BackupScheme = 'ecdsa-2p' | 'ecdsa-tn' | 'schnorr-2p';

export interface BackupCiphertext {
  kind: BackupCiphertextKind;
  curve: BackupCurve;
  scheme: BackupScheme;
  keyId: Uint8Array;
  threshold: number;
  shareCount: number;
  label: Uint8Array;
  blob: Uint8Array;
}

interface BackupCreateOptions {
  threshold?: number;
  shareCount?: number;
  label?: Uint8Array;
}

interface BackupCreateResult {
  ciphertext: BackupCiphertext;
  shares: Uint8Array[];
}

export async function runDkg(ctx: mpc.Ctx, opts: DkgOptions): Promise<DkgResult> {
  const runServerLocal = opts.mode !== 'device-only';
  const dkgOpts: Pick<mpc.DkgOptions, 'role' | 'keyId' | 'sessionId'> = {
    role: 'device',
    keyId: optionalClone(opts.keyId),
    sessionId: optionalClone(opts.sessionId),
  };

  const dkgDevice = mpc.dkgNew(ctx, dkgOpts);
  const dkgServer = runServerLocal ? mpc.dkgNew(ctx, { ...dkgOpts, role: 'server' }) : null;

  async function step(participant: Participant, handle: mpc.Dkg, inbound: Uint8Array | null) {
    const res = await mpc.dkgStep(ctx, handle, inbound ?? undefined);
    if (res.outMsg) {
      await opts.transport.send({ participant: participant === 'device' ? 'server' : 'device', payload: res.outMsg });
    }
    return res.done;
  }

  const waitForMessage = async (participant: Participant): Promise<Uint8Array> => {
    while (true) {
      const message = await opts.transport.receive(participant);
      if (message) {
        return message;
      }
      await new Promise((resolve) => setTimeout(resolve, 5));
    }
  };

  if (runServerLocal && dkgServer) {
    let deviceDone = false;
    let serverDone = false;

    for (let i = 0; i < 128 && !(deviceDone && serverDone); ++i) {
      if (!deviceDone) {
        const inbound = await waitForMessage('device');
        deviceDone = await step('device', dkgDevice, inbound);
      }
      if (!serverDone) {
        const inbound = await waitForMessage('server');
        serverDone = await step('server', dkgServer, inbound);
      }
    }
  } else {
    let inbound: Uint8Array | null = await waitForMessage('device');
    console.log('[maany-sdk] device-only dkg received first frame', inbound.length);
    for (let i = 0; i < 512; ++i) {
      const done = await step('device', dkgDevice, inbound);
      inbound = null;
      if (done) break;
      inbound = await waitForMessage('device');
      console.log('[maany-sdk] device-only dkg received frame', inbound.length, 'round', i + 1);
    }
  }

  const deviceKeypair = mpc.dkgFinalize(ctx, dkgDevice);
  let serverKeypair: mpc.Keypair | null = null;

  if (runServerLocal && dkgServer) {
    serverKeypair = mpc.dkgFinalize(ctx, dkgServer);
  }

  if (opts.storage) {
    const deviceBlob = mpc.kpExport(ctx, deviceKeypair);
    const keyId = toHex(deviceBlob);
    await opts.storage.save({ keyId, blob: cloneBytes(deviceBlob) });
    if (serverKeypair) {
      const serverBlob = mpc.kpExport(ctx, serverKeypair);
      await opts.storage.save({ keyId: `${keyId}:server`, blob: cloneBytes(serverBlob) });
    }
  }

  const backup = maybeCreateBackup(ctx, deviceKeypair, opts.backup);

  return { deviceKeypair, serverKeypair, backup };
}

function maybeCreateBackup(
  ctx: mpc.Ctx,
  deviceKeypair: mpc.Keypair,
  options?: DeviceBackupOptions
): DeviceBackupArtifacts | null {
  if (options?.enabled === false) {
    return null;
  }

  const backupOptions: BackupCreateOptions = {};
  if (typeof options?.threshold === 'number') {
    backupOptions.threshold = options.threshold;
  }
  if (typeof options?.shareCount === 'number') {
    backupOptions.shareCount = options.shareCount;
  }
  if (options?.label) {
    backupOptions.label = optionalClone(options.label);
  }

  const result = bindingWithBackup.backupCreate(ctx, deviceKeypair, backupOptions);
  return {
    ciphertext: cloneBackupCiphertext(result.ciphertext),
    shares: result.shares.map((share) => cloneBytes(share)),
  };
}

function cloneBackupCiphertext(ciphertext: BackupCiphertext): BackupCiphertext {
  return {
    kind: ciphertext.kind,
    curve: ciphertext.curve,
    scheme: ciphertext.scheme,
    keyId: cloneBytes(ciphertext.keyId),
    threshold: ciphertext.threshold,
    shareCount: ciphertext.shareCount,
    label: cloneBytes(ciphertext.label),
    blob: cloneBytes(ciphertext.blob),
  };
}
