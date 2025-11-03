import type { Transport, Participant } from '../transport';
import type { ShareStorage } from '../storage';
import * as mpc from '@maany/mpc-rn';
import { cloneBytes, optionalClone, toHex } from '../utils/bytes';

export interface DkgResult {
  deviceKeypair: mpc.Keypair;
  serverKeypair: mpc.Keypair;
}

export interface DkgOptions {
  transport: Transport;
  storage: ShareStorage;
  keyId?: Uint8Array;
  sessionId?: Uint8Array;
}

export async function runDkg(ctx: mpc.Ctx, opts: DkgOptions): Promise<DkgResult> {
  const dkgOpts: Pick<mpc.DkgOptions, 'role' | 'keyId' | 'sessionId'> = {
    role: 'device',
    keyId: optionalClone(opts.keyId),
    sessionId: optionalClone(opts.sessionId),
  };

  const dkgDevice = mpc.dkgNew(ctx, dkgOpts);
  const dkgServer = mpc.dkgNew(ctx, { ...dkgOpts, role: 'server' });

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
  const keyId = toHex(deviceBlob);

  await opts.storage.save({ keyId, blob: cloneBytes(deviceBlob) });
  await opts.storage.save({ keyId: `${keyId}:server`, blob: cloneBytes(serverBlob) });

  return { deviceKeypair, serverKeypair };
}
