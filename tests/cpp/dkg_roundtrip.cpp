#include "maany_mpc.h"

#include <cbmpc/crypto/base.h>
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

struct SignParticipant {
  maany_mpc_sign_t* sign{nullptr};
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

  std::vector<uint8_t> message(32);
  for (size_t i = 0; i < message.size(); ++i) message[i] = static_cast<uint8_t>(i + 1);

  maany_mpc_sign_opts_t sign_opts{};
  sign_opts.scheme = MAANY_MPC_SCHEME_ECDSA_2P;

  maany_mpc_sign_t* sign_device = nullptr;
  maany_mpc_sign_t* sign_server = nullptr;
  AbortOnError(maany_mpc_sign_new(ctx, device.kp, &sign_opts, &sign_device), "maany_mpc_sign_new(device)");
  AbortOnError(maany_mpc_sign_new(ctx, server.kp, &sign_opts, &sign_server), "maany_mpc_sign_new(server)");

  AbortOnError(maany_mpc_sign_set_message(ctx, sign_device, message.data(), message.size()),
               "maany_mpc_sign_set_message(device)");
  AbortOnError(maany_mpc_sign_set_message(ctx, sign_server, message.data(), message.size()),
               "maany_mpc_sign_set_message(server)");

  SignParticipant sign_dev{sign_device, false};
  SignParticipant sign_srv{sign_server, false};
  PendingMsg inbound_device_sign;
  PendingMsg inbound_server_sign;

  int sign_guard = 0;
  while (!(sign_dev.done && sign_srv.done)) {
    if (++sign_guard > 128) {
      std::fprintf(stderr, "Sign loop guard triggered\n");
      return 1;
    }

    if (!sign_srv.done) {
      maany_mpc_buf_t outbound{nullptr, 0};
      maany_mpc_step_result_t step{};
      AbortOnError(
          maany_mpc_sign_step(ctx, sign_srv.sign,
                              inbound_server_sign.buf.data ? &inbound_server_sign.buf : nullptr, &outbound, &step),
          "maany_mpc_sign_step(server)");
      inbound_server_sign.Reset(ctx);
      if (outbound.data) {
        inbound_device_sign.Reset(ctx);
        inbound_device_sign.buf = outbound;
      }
      sign_srv.done = (step == MAANY_MPC_STEP_DONE);
    }

    if (!sign_dev.done) {
      maany_mpc_buf_t outbound{nullptr, 0};
      maany_mpc_step_result_t step{};
      AbortOnError(
          maany_mpc_sign_step(ctx, sign_dev.sign,
                              inbound_device_sign.buf.data ? &inbound_device_sign.buf : nullptr, &outbound, &step),
          "maany_mpc_sign_step(device)");
      inbound_device_sign.Reset(ctx);
      if (outbound.data) {
        inbound_server_sign.Reset(ctx);
        inbound_server_sign.buf = outbound;
      }
      sign_dev.done = (step == MAANY_MPC_STEP_DONE);
    }
  }

  maany_mpc_buf_t sig_der{};
  AbortOnError(maany_mpc_sign_finalize(ctx, sign_device, MAANY_MPC_SIG_FORMAT_DER, &sig_der),
               "maany_mpc_sign_finalize(device)");

  maany_mpc_buf_t sig_raw{};
  AbortOnError(maany_mpc_sign_finalize(ctx, sign_device, MAANY_MPC_SIG_FORMAT_RAW_RS, &sig_raw),
               "maany_mpc_sign_finalize_raw(device)");

  if (sig_raw.len != 64) {
    std::fprintf(stderr, "Unexpected raw signature length\n");
    return 1;
  }

  coinbase::crypto::ecurve_t cb_curve = coinbase::crypto::curve_secp256k1;
  coinbase::crypto::ecc_point_t pub_point;
  auto rv = pub_point.from_bin(cb_curve,
                               coinbase::mem_t(pub_wrapper_device.pubkey.data,
                                               static_cast<int>(pub_wrapper_device.pubkey.len)));
  if (rv) {
    std::fprintf(stderr, "Failed to decode public key\n");
    return 1;
  }
  coinbase::crypto::ecc_pub_key_t pub_key(pub_point);
  rv = pub_key.verify(coinbase::mem_t(message.data(), static_cast<int>(message.size())),
                      coinbase::mem_t(sig_der.data, static_cast<int>(sig_der.len)));
  if (rv) {
    std::fprintf(stderr, "Signature verification failed\n");
    return 1;
  }

  coinbase::crypto::ecdsa_signature_t parsed_sig;
  rv = parsed_sig.from_der(cb_curve, coinbase::mem_t(sig_der.data, static_cast<int>(sig_der.len)));
  if (rv) {
    std::fprintf(stderr, "Failed to parse DER signature\n");
    return 1;
  }
  const auto order = cb_curve.order();
  int coord_size = order.get_bin_size();
  coinbase::buf_t r_bin = parsed_sig.get_r().to_bin(coord_size);
  coinbase::buf_t s_bin = parsed_sig.get_s().to_bin(coord_size);
  std::vector<uint8_t> expected_raw(static_cast<size_t>(coord_size) * 2);
  std::memcpy(expected_raw.data(), r_bin.data(), coord_size);
  std::memcpy(expected_raw.data() + coord_size, s_bin.data(), coord_size);
  r_bin.secure_bzero();
  s_bin.secure_bzero();
  if (sig_raw.len != expected_raw.size() ||
      std::memcmp(sig_raw.data, expected_raw.data(), expected_raw.size()) != 0) {
    std::fprintf(stderr, "Raw signature mismatch\n");
    return 1;
  }

  maany_mpc_buf_free(ctx, &sig_der);
  maany_mpc_buf_free(ctx, &sig_raw);

  maany_mpc_sign_free(sign_device);
  maany_mpc_sign_free(sign_server);

  maany_mpc_buf_free(ctx, &pub_wrapper_device.pubkey);
  maany_mpc_buf_free(ctx, &pub_wrapper_server.pubkey);

  maany_mpc_kp_free(device.kp);
  maany_mpc_kp_free(server.kp);
  maany_mpc_dkg_free(device.dkg);
  maany_mpc_dkg_free(server.dkg);
  inbound_device.Reset(ctx);
  inbound_server.Reset(ctx);

  inbound_device_sign.Reset(ctx);
  inbound_server_sign.Reset(ctx);

  maany_mpc_shutdown(ctx);
  return 0;
}
