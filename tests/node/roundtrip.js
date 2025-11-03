const path = require('node:path');

const binding = require(path.resolve(__dirname, '../../bindings/node'));

async function run() {
  const ctx = binding.init();
  try {
    const device = binding.dkgNew(ctx, { role: 'device' });
    const server = binding.dkgNew(ctx, { role: 'server' });

    let deviceMsg = null;
    let serverMsg = null;
    let deviceDone = false;
    let serverDone = false;
    for (let i = 0; i < 64 && !(deviceDone && serverDone); ++i) {
      if (!deviceDone) {
        const res = await binding.dkgStep(ctx, device, serverMsg);
        deviceMsg = res.outMsg ?? null;
        deviceDone = res.done;
      }
      if (!serverDone) {
        const res = await binding.dkgStep(ctx, server, deviceMsg);
        serverMsg = res.outMsg ?? null;
        serverDone = res.done;
      }
    }

    const deviceKp = binding.dkgFinalize(ctx, device);
    const serverKp = binding.dkgFinalize(ctx, server);

    const exported = binding.kpExport(ctx, deviceKp);
    const restored = binding.kpImport(ctx, exported);
    const pubOriginal = binding.kpPubkey(ctx, deviceKp);
    const pubRestored = binding.kpPubkey(ctx, restored);
    if (!pubOriginal.compressed.equals(pubRestored.compressed)) {
      throw new Error('Restored pubkey mismatch');
    }

    const message = Buffer.alloc(32, 1);
    const signDevice = binding.signNew(ctx, restored);
    const signServer = binding.signNew(ctx, serverKp);
    binding.signSetMessage(ctx, signDevice, message);
    binding.signSetMessage(ctx, signServer, message);

    let signDevMsg = null;
    let signSrvMsg = null;
    let signDevDone = false;
    let signSrvDone = false;
    for (let i = 0; i < 128 && !(signDevDone && signSrvDone); ++i) {
      if (!signSrvDone) {
        const res = await binding.signStep(ctx, signServer, signDevMsg);
        signSrvMsg = res.outMsg ?? null;
        signSrvDone = res.done;
      }
      if (!signDevDone) {
        const res = await binding.signStep(ctx, signDevice, signSrvMsg);
        signDevMsg = res.outMsg ?? null;
        signDevDone = res.done;
      }
    }

    const sigDer = binding.signFinalize(ctx, signDevice, 'der');
    if (sigDer.length === 0) throw new Error('Empty signature');
    binding.signFree(signDevice);
    binding.signFree(signServer);

    const refreshDevice = binding.refreshNew(ctx, restored);
    const refreshServer = binding.refreshNew(ctx, serverKp);

    let refDevMsg = null;
    let refSrvMsg = null;
    let refDevDone = false;
    let refSrvDone = false;
    for (let i = 0; i < 128 && !(refDevDone && refSrvDone); ++i) {
      if (!refDevDone) {
        const res = await binding.dkgStep(ctx, refreshDevice, refSrvMsg);
        refDevMsg = res.outMsg ?? null;
        refDevDone = res.done;
      }
      if (!refSrvDone) {
        const res = await binding.dkgStep(ctx, refreshServer, refDevMsg);
        refSrvMsg = res.outMsg ?? null;
        refSrvDone = res.done;
      }
    }

    const refreshedDeviceKp = binding.dkgFinalize(ctx, refreshDevice);
    binding.dkgFinalize(ctx, refreshServer);
    const pubRefreshed = binding.kpPubkey(ctx, refreshedDeviceKp);
    if (!pubOriginal.compressed.equals(pubRefreshed.compressed)) {
      throw new Error('Refresh changed public key');
    }

    binding.kpFree(refreshedDeviceKp);
    binding.kpFree(restored);
    binding.kpFree(serverKp);
    binding.kpFree(deviceKp);
    binding.dkgFree(device);
    binding.dkgFree(server);
    binding.dkgFree(refreshDevice);
    binding.dkgFree(refreshServer);

    console.log('Node MPC round-trip succeeded');
  } finally {
    binding.shutdown(ctx);
  }
}

run()
  .then(() => {
    process.exit(0);
  })
  .catch((err) => {
    console.error(err);
    process.exit(1);
  });
