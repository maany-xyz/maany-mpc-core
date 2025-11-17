import type { Transport, Participant } from '../transport';
import type { ShareStorage } from '../storage';
import * as mpc from '@maanyio/mpc-rn-bare';
import { cloneBytes, optionalClone, toHex } from '../utils/bytes';

export interface DkgResult {
  deviceKeypair: mpc.Keypair;
  serverKeypair: mpc.Keypair | null;
}

export interface DkgOptions {
  transport: Transport;
  storage: ShareStorage;
  keyId?: Uint8Array;
  sessionId?: Uint8Array;
  mode?: 'dual' | 'device-only';
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
    let inbound: Uint8Array | null = null;
    for (let i = 0; i < 512; ++i) {
      const done = await step('device', dkgDevice, inbound);
      inbound = null;
      if (done) break;
      inbound = await waitForMessage('device');
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

  return { deviceKeypair, serverKeypair };
}
