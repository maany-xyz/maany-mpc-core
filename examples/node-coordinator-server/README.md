# Node Coordinator Example

This example demonstrates how to consume `@maany/mpc-coordinator-node` from a plain
Node.js script. It simulates both the device and server participants using the
in-memory transport/storage helpers so you can exercise the full 2-of-2 DKG and
signing flow without any React Native code.

## Running

```bash
cd examples/node-coordinator-server
npm install
npm start
```

The script will:

1. Create an in-memory transport + storage.
2. Run the Maany coordinator to generate a new MPC keypair (device + server
   shares).
3. Derive the Cosmos address from the device share’s public key.
4. Produce a dummy sign-doc, hash it, and run the 2-of-2 signing round.
5. Print the resulting DER signature in hex.

You can use this as a reference when wiring the coordinator into a real backend
service (e.g., replace the in-memory transport with WebSocket transports and
persist shares using your database or KMS).
