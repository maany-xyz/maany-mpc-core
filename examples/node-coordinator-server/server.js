const {
  createCoordinator,
  InMemoryTransport,
  InMemoryShareStorage,
  pubkeyToCosmosAddress,
  makeSignBytes,
  sha256,
} = require('@maany/mpc-coordinator-node');
const mpc = require('@maany/mpc-node');

async function main() {
  const transport = new InMemoryTransport();
  const storage = new InMemoryShareStorage();
  const coordinator = createCoordinator({ transport, storage });

  const ctx = coordinator.initContext();
  console.log('Running 2-of-2 DKG locally (device + server simulated)...');
  const { deviceKeypair, serverKeypair } = await coordinator.runDkg(ctx, {});

  const pub = mpc.kpPubkey(ctx, deviceKeypair);
  const address = pubkeyToCosmosAddress(pub.compressed);
  console.log('Derived Cosmos address:', address);

  const signDoc = makeSignBytes({
    chainId: 'cosmoshub-4',
    accountNumber: '0',
    sequence: '0',
    bodyBytes: Buffer.alloc(32, 0x42),
    authInfoBytes: Buffer.alloc(32, 0x24),
  });
  const digest = sha256(signDoc);
  const signature = await coordinator.runSign(ctx, deviceKeypair, serverKeypair, { message: digest });
  console.log('DER signature (hex):', Buffer.from(signature).toString('hex'));

  mpc.shutdown(ctx);
}

main().catch((err) => {
  console.error('Example failed:', err);
  process.exitCode = 1;
});
