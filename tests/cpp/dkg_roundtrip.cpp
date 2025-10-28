#include "maany_mpc.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

void AbortOnError(maany_mpc_error_t err, const char* where) {
  if (err == MAANY_MPC_OK) return;
  std::fprintf(stderr, "%s failed: %s (%d)\n", where, maany_mpc_error_string(err), static_cast<int>(err));
  std::exit(1);
}

struct PendingMsg {
  maany_mpc_buf_t buf{nullptr, 0};

  void Reset(maany_mpc_ctx_t* ctx) {
    if (!buf.data) return;
    maany_mpc_buf_free(ctx, &buf);
    buf.data = nullptr;
    buf.len = 0;
  }
};

struct Participant {
  maany_mpc_dkg_t* dkg{nullptr};
  maany_mpc_keypair_t* kp{nullptr};
  bool done{false};
};

}  // namespace

int main() {
  maany_mpc_ctx_t* ctx = maany_mpc_init(nullptr);
  if (!ctx) {
    std::fprintf(stderr, "maany_mpc_init failed\n");
    return 1;
  }

  maany_mpc_dkg_opts_t opts_device{};
  opts_device.curve = MAANY_MPC_CURVE_SECP256K1;
  opts_device.scheme = MAANY_MPC_SCHEME_ECDSA_2P;
  opts_device.kind = MAANY_MPC_SHARE_DEVICE;

  maany_mpc_dkg_opts_t opts_server = opts_device;
  opts_server.kind = MAANY_MPC_SHARE_SERVER;

  Participant device;
  Participant server;

  AbortOnError(maany_mpc_dkg_new(ctx, &opts_device, &device.dkg), "maany_mpc_dkg_new(device)");
  AbortOnError(maany_mpc_dkg_new(ctx, &opts_server, &server.dkg), "maany_mpc_dkg_new(server)");

  PendingMsg inbound_device;
  PendingMsg inbound_server;

  int guard = 0;
  while (!(device.done && server.done)) {
    if (++guard > 64) {
      std::fprintf(stderr, "DKG loop guard triggered\n");
      return 1;
    }

    if (!device.done) {
      maany_mpc_buf_t outbound{nullptr, 0};
      maany_mpc_step_result_t step{};
      AbortOnError(
          maany_mpc_dkg_step(ctx, device.dkg, inbound_device.buf.data ? &inbound_device.buf : nullptr, &outbound, &step),
          "maany_mpc_dkg_step(device)");
      inbound_device.Reset(ctx);
      if (outbound.data) {
        inbound_server.Reset(ctx);
        inbound_server.buf = outbound;
      }
      device.done = (step == MAANY_MPC_STEP_DONE);
    }

    if (!server.done) {
      maany_mpc_buf_t outbound{nullptr, 0};
      maany_mpc_step_result_t step{};
      AbortOnError(
          maany_mpc_dkg_step(ctx, server.dkg, inbound_server.buf.data ? &inbound_server.buf : nullptr, &outbound, &step),
          "maany_mpc_dkg_step(server)");
      inbound_server.Reset(ctx);
      if (outbound.data) {
        inbound_device.Reset(ctx);
        inbound_device.buf = outbound;
      }
      server.done = (step == MAANY_MPC_STEP_DONE);
    }
  }

  AbortOnError(maany_mpc_dkg_finalize(ctx, device.dkg, &device.kp), "maany_mpc_dkg_finalize(device)");
  AbortOnError(maany_mpc_dkg_finalize(ctx, server.dkg, &server.kp), "maany_mpc_dkg_finalize(server)");

  maany_mpc_buf_t pub_device{};
  maany_mpc_pubkey_t pub_wrapper_device{};
  pub_wrapper_device.pubkey = pub_device;
  AbortOnError(maany_mpc_kp_pubkey(ctx, device.kp, &pub_wrapper_device), "maany_mpc_kp_pubkey(device)");

  maany_mpc_buf_t pub_server{};
  maany_mpc_pubkey_t pub_wrapper_server{};
  pub_wrapper_server.pubkey = pub_server;
  AbortOnError(maany_mpc_kp_pubkey(ctx, server.kp, &pub_wrapper_server), "maany_mpc_kp_pubkey(server)");

  if (pub_wrapper_device.pubkey.len != pub_wrapper_server.pubkey.len ||
      std::memcmp(pub_wrapper_device.pubkey.data, pub_wrapper_server.pubkey.data, pub_wrapper_device.pubkey.len) != 0) {
    std::fprintf(stderr, "Public keys differ\n");
    return 1;
  }

  maany_mpc_buf_free(ctx, &pub_wrapper_device.pubkey);
  maany_mpc_buf_free(ctx, &pub_wrapper_server.pubkey);

  maany_mpc_kp_free(device.kp);
  maany_mpc_kp_free(server.kp);
  maany_mpc_dkg_free(device.dkg);
  maany_mpc_dkg_free(server.dkg);
  inbound_device.Reset(ctx);
  inbound_server.Reset(ctx);

  maany_mpc_shutdown(ctx);
  return 0;
}
