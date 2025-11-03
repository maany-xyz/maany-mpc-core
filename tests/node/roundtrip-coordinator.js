const path = require('node:path');
const coordinatorPkg = require('../../packages/coordinator/dist');
const transportPkg = require('../../packages/coordinator/dist/transport');
const storagePkg = require('../../packages/coordinator/dist/storage');
const { createCoordinator, pubkeyToCosmosAddress, makeSignBytes, sha256 } = coordinatorPkg;
const transport = new transportPkg.InMemoryTransport();
const storage = new storagePkg.InMemoryShareStorage();
const coordinator = createCoordinator({ transport, storage });

(async () => {
  const ctx = coordinator.initContext();
  const { deviceKeypair, serverKeypair } = await coordinator.runDkg(ctx, {});
  const binding = require('../../bindings/node');
  const pub = binding.kpPubkey(ctx, deviceKeypair);
  const address = pubkeyToCosmosAddress(pub.compressed);
  console.log('Address:', address);
  const message = Buffer.alloc(32, 0x42);
  const signBytes = makeSignBytes({
    chainId: 'cosmoshub-4',
    accountNumber: '0',
    sequence: '0',
    bodyBytes: message,
    authInfoBytes: message,
  });
  const digest = sha256(signBytes);
  const signature = await coordinator.runSign(ctx, deviceKeypair, serverKeypair, { message: digest });
  console.log('Signature length:', signature.length);
})();
