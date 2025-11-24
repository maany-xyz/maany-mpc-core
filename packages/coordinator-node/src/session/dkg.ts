import type { Transport, Participant } from '../transport';
import type { CoordinatorStorage, WalletShareUpsert } from '../storage';
import type { KeyEncryptor } from '../crypto/key-encryptor';
import * as mpc from '@maany/mpc-node';

export interface DkgResult {
  deviceKeypair: mpc.Keypair | null;
  serverKeypair: mpc.Keypair;
}

export interface DkgOptions {
  transport: Transport;
  storage: CoordinatorStorage;
  encryptor: KeyEncryptor;
  keyId?: Uint8Array | Buffer;
  sessionId?: Uint8Array | Buffer;
  mode?: 'dual' | 'server-only';
  metadata?: {
    walletId?: string;
    appId?: string;
    userId?: string;
    deviceId?: string;
  };
}

export async function runDkg(ctx: mpc.Ctx, opts: DkgOptions): Promise<DkgResult> {
  const simulateDevice = opts.mode !== 'server-only';
  console.log('[runDkg] mode =', opts.mode ?? 'dual', 'simulateDevice =', simulateDevice);
  const normalizedKeyId = opts.keyId ? Buffer.from(opts.keyId) : undefined;
  const normalizedSessionId = opts.sessionId ? Buffer.from(opts.sessionId) : undefined;

  const dkgServer = mpc.dkgNew(ctx, {
    role: 'server',
    keyId: normalizedKeyId,
    sessionId: normalizedSessionId,
  });

  let deviceKeypair: mpc.Keypair | null = null;

  if (simulateDevice) {
    const dkgDevice = mpc.dkgNew(ctx, {
      role: 'device',
      keyId: normalizedKeyId,
      sessionId: normalizedSessionId,
    });

    async function step(participant: Participant, handle: mpc.Dkg, inbound: Uint8Array | null) {
      const res = await mpc.dkgStep(ctx, handle, inbound ?? undefined);
      if (res.outMsg) {
        await opts.transport.send({ participant: participant === 'device' ? 'server' : 'device', payload: res.outMsg });
      }
      return res.done;
    }

    let deviceDone = false;
    let serverDone = false;

    for (let i = 0; i < 128 && !(deviceDone && serverDone); ++i) {
      if (!deviceDone) {
        const inbound = await opts.transport.receive('device');
        deviceDone = await step('device', dkgDevice, inbound);
      }
      if (!serverDone) {
        const inbound = await opts.transport.receive('server');
        serverDone = await step('server', dkgServer, inbound);
      }
    }

    deviceKeypair = mpc.dkgFinalize(ctx, dkgDevice);
  } else {
    const waitForDeviceMessage = async (): Promise<Uint8Array> => {
      while (true) {
        const next = await opts.transport.receive('server');
        if (next) {
          console.log(`[server-only dkg] received ${next.length} bytes from device`);
          return next;
        }
        console.log('[server-only dkg] waiting for device frame...');
        await new Promise((resolve) => setTimeout(resolve, 5));
      }
    };

    let inbound: Uint8Array | undefined;
    for (let i = 0; i < 256; ++i) {
      console.log(`[server-only dkg] round=${i} inbound=${inbound ? inbound.length : 0}`);
      const res = await mpc.dkgStep(ctx, dkgServer, inbound);
      inbound = undefined;
      if (res.outMsg) {
        console.log(`[server-only dkg] sending ${res.outMsg.length} bytes to device`);
        await opts.transport.send({ participant: 'device', payload: res.outMsg });
      }
      if (res.done) {
        console.log('[server-only dkg] done');
        break;
      }
      inbound = await waitForDeviceMessage();
    }
  }

  const serverKeypair = mpc.dkgFinalize(ctx, dkgServer);

  const serverBlob = mpc.kpExport(ctx, serverKeypair);
  const walletId =
    opts.metadata?.walletId ??
    (normalizedKeyId ? Buffer.from(normalizedKeyId).toString('hex') : Buffer.from(serverBlob).toString('hex'));
  const publicKey = Buffer.from(mpc.kpPubkey(ctx, serverKeypair).compressed).toString('hex');
  const encryptedServerShare = await opts.encryptor.encryptShare(serverBlob);

  const record: WalletShareUpsert = {
    walletId,
    appId: opts.metadata?.appId,
    userId: opts.metadata?.userId,
    deviceId: opts.metadata?.deviceId,
    publicKey,
    encryptedServerShare,
  };

  const existing = await opts.storage.getWalletShare(walletId);
  if (existing) {
    await opts.storage.updateWalletShare(record);
  } else {
    await opts.storage.saveWalletShare(record);
  }

  return { deviceKeypair, serverKeypair };
}
