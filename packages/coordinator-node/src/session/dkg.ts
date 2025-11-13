import type { Transport, Participant } from '../transport';
import type { ShareStorage } from '../storage';
import * as mpc from '@maany/mpc-node';

export interface DkgResult {
  deviceKeypair: mpc.Keypair;
  serverKeypair: mpc.Keypair;
}

export interface DkgOptions {
  transport: Transport;
  storage: ShareStorage;
  keyId?: Uint8Array | Buffer;
  sessionId?: Uint8Array | Buffer;
}

export async function runDkg(ctx: mpc.Ctx, opts: DkgOptions): Promise<DkgResult> {
  const dkgDevice = mpc.dkgNew(ctx, {
    role: 'device',
    keyId: opts.keyId ? Buffer.from(opts.keyId) : undefined,
    sessionId: opts.sessionId ? Buffer.from(opts.sessionId) : undefined,
  });
  const dkgServer = mpc.dkgNew(ctx, {
    role: 'server',
    keyId: opts.keyId ? Buffer.from(opts.keyId) : undefined,
    sessionId: opts.sessionId ? Buffer.from(opts.sessionId) : undefined,
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

  const deviceKeypair = mpc.dkgFinalize(ctx, dkgDevice);
  const serverKeypair = mpc.dkgFinalize(ctx, dkgServer);

  const deviceBlob = mpc.kpExport(ctx, deviceKeypair);
  const serverBlob = mpc.kpExport(ctx, serverKeypair);
  const keyId = Buffer.from(deviceBlob).toString('hex');
  await opts.storage.save({ keyId, blob: deviceBlob });
  await opts.storage.save({ keyId: `${keyId}:server`, blob: serverBlob });

  return { deviceKeypair, serverKeypair };
}
