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

bool RunSign(maany_mpc_ctx_t* ctx, maany_mpc_sign_t* sign_device, maany_mpc_sign_t* sign_server,
             const char* label) {
  SignParticipant device{sign_device, false};
  SignParticipant server{sign_server, false};
  PendingMsg inbound_device;
  PendingMsg inbound_server;

  int guard = 0;
  while (!(device.done && server.done)) {
    if (++guard > 128) {
      std::fprintf(stderr, "%s loop guard triggered\n", label);
      return false;
    }

    if (!server.done) {
      maany_mpc_buf_t outbound{nullptr, 0};
      maany_mpc_step_result_t step{};
      maany_mpc_error_t err = maany_mpc_sign_step(ctx, server.sign,
                                                  inbound_server.buf.data ? &inbound_server.buf : nullptr, &outbound,
                                                  &step);
      inbound_server.Reset(ctx);
      if (err != MAANY_MPC_OK) {
        std::fprintf(stderr, "%s server step failed: %s (%d)\n", label, maany_mpc_error_string(err), err);
        return false;
      }
      if (outbound.data) {
        inbound_device.Reset(ctx);
        inbound_device.buf = outbound;
      }
      server.done = (step == MAANY_MPC_STEP_DONE);
    }

    if (!device.done) {
      maany_mpc_buf_t outbound{nullptr, 0};
      maany_mpc_step_result_t step{};
      maany_mpc_error_t err = maany_mpc_sign_step(ctx, device.sign,
                                                  inbound_device.buf.data ? &inbound_device.buf : nullptr, &outbound,
                                                  &step);
      inbound_device.Reset(ctx);
      if (err != MAANY_MPC_OK) {
        std::fprintf(stderr, "%s device step failed: %s (%d)\n", label, maany_mpc_error_string(err), err);
        return false;
      }
      if (outbound.data) {
        inbound_server.Reset(ctx);
        inbound_server.buf = outbound;
      }
      device.done = (step == MAANY_MPC_STEP_DONE);
    }
  }

  inbound_device.Reset(ctx);
  inbound_server.Reset(ctx);
  return true;
}

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

  maany_mpc_buf_t exported_device{nullptr, 0};
  maany_mpc_buf_t exported_server{nullptr, 0};
  AbortOnError(maany_mpc_kp_export(ctx, device.kp, &exported_device), "maany_mpc_kp_export(device)");
  AbortOnError(maany_mpc_kp_export(ctx, server.kp, &exported_server), "maany_mpc_kp_export(server)");

  maany_mpc_kp_free(device.kp);
  maany_mpc_kp_free(server.kp);
  device.kp = nullptr;
  server.kp = nullptr;

  AbortOnError(maany_mpc_kp_import(ctx, &exported_device, &device.kp), "maany_mpc_kp_import(device)");
  AbortOnError(maany_mpc_kp_import(ctx, &exported_server, &server.kp), "maany_mpc_kp_import(server)");
  maany_mpc_buf_free(ctx, &exported_device);
  maany_mpc_buf_free(ctx, &exported_server);

  maany_mpc_buf_t restored_pub_device{};
  maany_mpc_pubkey_t restored_pub_wrapper_device{};
  restored_pub_wrapper_device.pubkey = restored_pub_device;
  AbortOnError(maany_mpc_kp_pubkey(ctx, device.kp, &restored_pub_wrapper_device),
               "maany_mpc_kp_pubkey(restored_device)");

  maany_mpc_buf_t restored_pub_server{};
  maany_mpc_pubkey_t restored_pub_wrapper_server{};
  restored_pub_wrapper_server.pubkey = restored_pub_server;
  AbortOnError(maany_mpc_kp_pubkey(ctx, server.kp, &restored_pub_wrapper_server),
               "maany_mpc_kp_pubkey(restored_server)");

  if (restored_pub_wrapper_device.pubkey.len != pub_wrapper_device.pubkey.len ||
      std::memcmp(restored_pub_wrapper_device.pubkey.data, pub_wrapper_device.pubkey.data,
                  restored_pub_wrapper_device.pubkey.len) != 0) {
    std::fprintf(stderr, "Restored device pubkey mismatch\n");
    return 1;
  }
  if (restored_pub_wrapper_server.pubkey.len != pub_wrapper_server.pubkey.len ||
      std::memcmp(restored_pub_wrapper_server.pubkey.data, pub_wrapper_server.pubkey.data,
                  restored_pub_wrapper_server.pubkey.len) != 0) {
    std::fprintf(stderr, "Restored server pubkey mismatch\n");
    return 1;
  }

  maany_mpc_buf_free(ctx, &restored_pub_wrapper_device.pubkey);
  maany_mpc_buf_free(ctx, &restored_pub_wrapper_server.pubkey);

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

  if (!RunSign(ctx, sign_device, sign_server, "Sign")) return 1;

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

  maany_mpc_refresh_opts_t refresh_opts{};
  Participant refresh_dev{};
  Participant refresh_srv{};
  AbortOnError(maany_mpc_refresh_new(ctx, device.kp, &refresh_opts, &refresh_dev.dkg),
               "maany_mpc_refresh_new(device)");
  AbortOnError(maany_mpc_refresh_new(ctx, server.kp, &refresh_opts, &refresh_srv.dkg),
               "maany_mpc_refresh_new(server)");

  PendingMsg inbound_device_refresh;
  PendingMsg inbound_server_refresh;

  int refresh_guard = 0;
  while (!(refresh_dev.done && refresh_srv.done)) {
    if (++refresh_guard > 128) {
      std::fprintf(stderr, "Refresh loop guard triggered\n");
      return 1;
    }

    if (!refresh_dev.done) {
      maany_mpc_buf_t outbound{nullptr, 0};
      maany_mpc_step_result_t step{};
      AbortOnError(
          maany_mpc_dkg_step(ctx, refresh_dev.dkg,
                              inbound_device_refresh.buf.data ? &inbound_device_refresh.buf : nullptr, &outbound, &step),
          "maany_mpc_refresh_step(device)");
      inbound_device_refresh.Reset(ctx);
      if (outbound.data) {
        inbound_server_refresh.Reset(ctx);
        inbound_server_refresh.buf = outbound;
      }
      refresh_dev.done = (step == MAANY_MPC_STEP_DONE);
    }

    if (!refresh_srv.done) {
      maany_mpc_buf_t outbound{nullptr, 0};
      maany_mpc_step_result_t step{};
      AbortOnError(
          maany_mpc_dkg_step(ctx, refresh_srv.dkg,
                              inbound_server_refresh.buf.data ? &inbound_server_refresh.buf : nullptr, &outbound, &step),
          "maany_mpc_refresh_step(server)");
      inbound_server_refresh.Reset(ctx);
      if (outbound.data) {
        inbound_device_refresh.Reset(ctx);
        inbound_device_refresh.buf = outbound;
      }
      refresh_srv.done = (step == MAANY_MPC_STEP_DONE);
    }
  }

  maany_mpc_keypair_t* refreshed_device_kp = nullptr;
  maany_mpc_keypair_t* refreshed_server_kp = nullptr;
  AbortOnError(maany_mpc_dkg_finalize(ctx, refresh_dev.dkg, &refreshed_device_kp),
               "maany_mpc_refresh_finalize(device)");
  AbortOnError(maany_mpc_dkg_finalize(ctx, refresh_srv.dkg, &refreshed_server_kp),
               "maany_mpc_refresh_finalize(server)");
  maany_mpc_dkg_free(refresh_dev.dkg);
  maany_mpc_dkg_free(refresh_srv.dkg);
  inbound_device_refresh.Reset(ctx);
  inbound_server_refresh.Reset(ctx);

  maany_mpc_buf_t refreshed_pub_device{};
  maany_mpc_pubkey_t refreshed_pub_wrapper_device{};
  refreshed_pub_wrapper_device.pubkey = refreshed_pub_device;
  AbortOnError(maany_mpc_kp_pubkey(ctx, refreshed_device_kp, &refreshed_pub_wrapper_device),
               "maany_mpc_kp_pubkey(refreshed_device)");

  maany_mpc_buf_t refreshed_pub_server{};
  maany_mpc_pubkey_t refreshed_pub_wrapper_server{};
  refreshed_pub_wrapper_server.pubkey = refreshed_pub_server;
  AbortOnError(maany_mpc_kp_pubkey(ctx, refreshed_server_kp, &refreshed_pub_wrapper_server),
               "maany_mpc_kp_pubkey(refreshed_server)");

  if (refreshed_pub_wrapper_device.pubkey.len != pub_wrapper_device.pubkey.len ||
      std::memcmp(refreshed_pub_wrapper_device.pubkey.data, pub_wrapper_device.pubkey.data,
                  refreshed_pub_wrapper_device.pubkey.len) != 0) {
    std::fprintf(stderr, "Refreshed device pubkey mismatch\n");
    return 1;
  }
  if (refreshed_pub_wrapper_server.pubkey.len != pub_wrapper_server.pubkey.len ||
      std::memcmp(refreshed_pub_wrapper_server.pubkey.data, pub_wrapper_server.pubkey.data,
                  refreshed_pub_wrapper_server.pubkey.len) != 0) {
    std::fprintf(stderr, "Refreshed server pubkey mismatch\n");
    return 1;
  }

  maany_mpc_keypair_t* old_device_kp = device.kp;
  maany_mpc_keypair_t* old_server_kp = server.kp;
  device.kp = refreshed_device_kp;
  server.kp = refreshed_server_kp;
  maany_mpc_kp_free(old_device_kp);
  maany_mpc_kp_free(old_server_kp);

  maany_mpc_buf_free(ctx, &pub_wrapper_device.pubkey);
  maany_mpc_buf_free(ctx, &pub_wrapper_server.pubkey);

  maany_mpc_sign_opts_t sign_opts_refresh{};
  sign_opts_refresh.scheme = MAANY_MPC_SCHEME_ECDSA_2P;

  maany_mpc_sign_t* sign_device_refresh = nullptr;
  maany_mpc_sign_t* sign_server_refresh = nullptr;
  AbortOnError(maany_mpc_sign_new(ctx, device.kp, &sign_opts_refresh, &sign_device_refresh),
               "maany_mpc_sign_new(device_refresh)");
  AbortOnError(maany_mpc_sign_new(ctx, server.kp, &sign_opts_refresh, &sign_server_refresh),
               "maany_mpc_sign_new(server_refresh)");

  AbortOnError(maany_mpc_sign_set_message(ctx, sign_device_refresh, message.data(), message.size()),
               "maany_mpc_sign_set_message(device_refresh)");
  AbortOnError(maany_mpc_sign_set_message(ctx, sign_server_refresh, message.data(), message.size()),
               "maany_mpc_sign_set_message(server_refresh)");

  if (!RunSign(ctx, sign_device_refresh, sign_server_refresh, "Sign (refresh)")) return 1;

  maany_mpc_buf_t sig_der_refresh{};
  AbortOnError(maany_mpc_sign_finalize(ctx, sign_device_refresh, MAANY_MPC_SIG_FORMAT_DER, &sig_der_refresh),
               "maany_mpc_sign_finalize(device_refresh)");
  maany_mpc_buf_t sig_raw_refresh{};
  AbortOnError(maany_mpc_sign_finalize(ctx, sign_device_refresh, MAANY_MPC_SIG_FORMAT_RAW_RS, &sig_raw_refresh),
               "maany_mpc_sign_finalize_raw(device_refresh)");

  if (sig_raw_refresh.len != 64) {
    std::fprintf(stderr, "Unexpected raw signature length after refresh\n");
    return 1;
  }

  auto rv_refresh = pub_key.verify(coinbase::mem_t(message.data(), static_cast<int>(message.size())),
                                   coinbase::mem_t(sig_der_refresh.data, static_cast<int>(sig_der_refresh.len)));
  if (rv_refresh) {
    std::fprintf(stderr, "Signature verification failed after refresh\n");
    return 1;
  }

  coinbase::crypto::ecdsa_signature_t parsed_sig_refresh;
  rv_refresh = parsed_sig_refresh.from_der(cb_curve,
                                           coinbase::mem_t(sig_der_refresh.data,
                                                           static_cast<int>(sig_der_refresh.len)));
  if (rv_refresh) {
    std::fprintf(stderr, "Failed to parse DER signature after refresh\n");
    return 1;
  }
  coinbase::buf_t r_bin_refresh = parsed_sig_refresh.get_r().to_bin(coord_size);
  coinbase::buf_t s_bin_refresh = parsed_sig_refresh.get_s().to_bin(coord_size);
  std::vector<uint8_t> expected_raw_refresh(static_cast<size_t>(coord_size) * 2);
  std::memcpy(expected_raw_refresh.data(), r_bin_refresh.data(), coord_size);
  std::memcpy(expected_raw_refresh.data() + coord_size, s_bin_refresh.data(), coord_size);
  r_bin_refresh.secure_bzero();
  s_bin_refresh.secure_bzero();
  if (sig_raw_refresh.len != expected_raw_refresh.size() ||
      std::memcmp(sig_raw_refresh.data, expected_raw_refresh.data(), expected_raw_refresh.size()) != 0) {
    std::fprintf(stderr, "Raw signature mismatch after refresh\n");
    return 1;
  }

  maany_mpc_buf_free(ctx, &sig_der_refresh);
  maany_mpc_buf_free(ctx, &sig_raw_refresh);

  maany_mpc_sign_free(sign_device_refresh);
  maany_mpc_sign_free(sign_server_refresh);

  maany_mpc_buf_free(ctx, &refreshed_pub_wrapper_device.pubkey);
  maany_mpc_buf_free(ctx, &refreshed_pub_wrapper_server.pubkey);

  maany_mpc_kp_free(device.kp);
  maany_mpc_kp_free(server.kp);
  maany_mpc_dkg_free(device.dkg);
  maany_mpc_dkg_free(server.dkg);
  inbound_device.Reset(ctx);
  inbound_server.Reset(ctx);

  maany_mpc_shutdown(ctx);
  return 0;
}
