#include <node_api.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "maany_mpc.h"

namespace {

struct CtxHandle {
  maany_mpc_ctx_t* ctx;
};

struct DkgHandle {
  maany_mpc_dkg_t* dkg;
};

struct KeypairHandle {
  maany_mpc_keypair_t* kp;
};

struct DeferredWorkBase {
  napi_env env;
  napi_deferred deferred;
  napi_async_work work;
  maany_mpc_error_t status{MAANY_MPC_OK};
  std::string error_context;
};

napi_value CreateError(napi_env env, const std::string& context, maany_mpc_error_t err) {
  const char* err_str = maany_mpc_error_string(err);
  std::string message = context;
  message.append(": ");
  message.append(err_str ? err_str : "unknown");

  napi_value msg;
  napi_status status = napi_create_string_utf8(env, message.c_str(), message.size(), &msg);
  if (status != napi_ok) return nullptr;

  napi_value code;
  status = napi_create_string_utf8(env, "ERR_MAANY_MPC", NAPI_AUTO_LENGTH, &code);
  if (status != napi_ok) return nullptr;

  napi_value error;
  status = napi_create_error(env, code, msg, &error);
  if (status != napi_ok) return nullptr;

  return error;
}

void FinalizeCtx(napi_env /*env*/, void* data, void* /*hint*/) {
  auto* handle = static_cast<CtxHandle*>(data);
  if (!handle) return;
  if (handle->ctx) {
    maany_mpc_shutdown(handle->ctx);
    handle->ctx = nullptr;
  }
  delete handle;
}

void FinalizeDkg(napi_env /*env*/, void* data, void* /*hint*/) {
  auto* handle = static_cast<DkgHandle*>(data);
  if (!handle) return;
  if (handle->dkg) {
    maany_mpc_dkg_free(handle->dkg);
    handle->dkg = nullptr;
  }
  delete handle;
}

void FinalizeKeypair(napi_env /*env*/, void* data, void* /*hint*/) {
  auto* handle = static_cast<KeypairHandle*>(data);
  if (!handle) return;
  if (handle->kp) {
    maany_mpc_kp_free(handle->kp);
    handle->kp = nullptr;
  }
  delete handle;
}

template <typename T>
napi_value WrapHandle(napi_env env, T* handle, napi_finalize finalizer) {
  napi_value object;
  napi_status status = napi_create_object(env, &object);
  if (status != napi_ok) return nullptr;

  status = napi_wrap(env, object, handle, finalizer, nullptr, nullptr);
  if (status != napi_ok) return nullptr;

  return object;
}

template <typename T>
bool UnwrapHandle(napi_env env, napi_value value, T** out_handle) {
  void* data = nullptr;
  if (napi_unwrap(env, value, &data) != napi_ok || data == nullptr) {
    napi_throw_type_error(env, nullptr, "Invalid handle object");
    return false;
  }
  *out_handle = static_cast<T*>(data);
  return true;
}

napi_value JsKpFree(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "kpFree expects a keypair handle");
    return nullptr;
  }

  KeypairHandle* kp_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &kp_handle)) return nullptr;
  if (kp_handle->kp) {
    maany_mpc_kp_free(kp_handle->kp);
    kp_handle->kp = nullptr;
  }
  return nullptr;
}

napi_value JsDkgFree(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "dkgFree expects a DKG handle");
    return nullptr;
  }

  DkgHandle* dkg_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &dkg_handle)) return nullptr;
  if (dkg_handle->dkg) {
    maany_mpc_dkg_free(dkg_handle->dkg);
    dkg_handle->dkg = nullptr;
  }
  return nullptr;
}

napi_value JsInit(napi_env env, napi_callback_info info) {
  size_t argc = 0;
  napi_value argv[1];
  napi_value this_arg;
  napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr);

  maany_mpc_ctx_t* ctx = maany_mpc_init(nullptr);
  if (!ctx) {
    napi_throw_error(env, nullptr, "maany_mpc_init failed");
    return nullptr;
  }

  auto* handle = new CtxHandle{ctx};
  napi_value result = WrapHandle(env, handle, FinalizeCtx);
  if (!result) {
    FinalizeCtx(env, handle, nullptr);
    napi_throw_error(env, nullptr, "Failed to wrap context handle");
    return nullptr;
  }
  return result;
}

napi_value JsShutdown(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "shutdown expects a context handle");
    return nullptr;
  }

  CtxHandle* ctx_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &ctx_handle)) return nullptr;
  if (ctx_handle->ctx) {
    maany_mpc_shutdown(ctx_handle->ctx);
    ctx_handle->ctx = nullptr;
  }
  return nullptr;
}

maany_mpc_share_kind_t ParseRole(napi_env env, napi_value value) {
  size_t length = 0;
  napi_get_value_string_utf8(env, value, nullptr, 0, &length);
  std::string role(length, '\0');
  napi_get_value_string_utf8(env, value, role.data(), role.size() + 1, &length);
  role.resize(length);

  if (role == "device") return MAANY_MPC_SHARE_DEVICE;
  if (role == "server") return MAANY_MPC_SHARE_SERVER;
  napi_throw_range_error(env, nullptr, "role must be 'device' or 'server'");
  return static_cast<maany_mpc_share_kind_t>(-1);
}

const char* ShareKindToString(maany_mpc_share_kind_t kind) {
  switch (kind) {
    case MAANY_MPC_SHARE_DEVICE:
      return "device";
    case MAANY_MPC_SHARE_SERVER:
      return "server";
    default:
      return "unknown";
  }
}

maany_mpc_share_kind_t ShareKindFromString(napi_env env, napi_value value) {
  maany_mpc_share_kind_t kind = ParseRole(env, value);
  return kind;
}

const char* CurveToString(maany_mpc_curve_t curve) {
  switch (curve) {
    case MAANY_MPC_CURVE_SECP256K1:
      return "secp256k1";
    case MAANY_MPC_CURVE_ED25519:
      return "ed25519";
    default:
      return "unknown";
  }
}

maany_mpc_curve_t CurveFromString(napi_env env, napi_value value) {
  size_t length = 0;
  napi_get_value_string_utf8(env, value, nullptr, 0, &length);
  std::string curve(length, '\0');
  napi_get_value_string_utf8(env, value, curve.data(), curve.size() + 1, &length);
  curve.resize(length);
  if (curve == "secp256k1") return MAANY_MPC_CURVE_SECP256K1;
  if (curve == "ed25519") return MAANY_MPC_CURVE_ED25519;
  napi_throw_range_error(env, nullptr, "unsupported curve value");
  return static_cast<maany_mpc_curve_t>(-1);
}

const char* SchemeToString(maany_mpc_scheme_t scheme) {
  switch (scheme) {
    case MAANY_MPC_SCHEME_ECDSA_2P:
      return "ecdsa-2p";
    case MAANY_MPC_SCHEME_ECDSA_TN:
      return "ecdsa-tn";
    case MAANY_MPC_SCHEME_SCHNORR_2P:
      return "schnorr-2p";
    default:
      return "unknown";
  }
}

maany_mpc_scheme_t SchemeFromString(napi_env env, napi_value value) {
  size_t length = 0;
  napi_get_value_string_utf8(env, value, nullptr, 0, &length);
  std::string scheme(length, '\0');
  napi_get_value_string_utf8(env, value, scheme.data(), scheme.size() + 1, &length);
  scheme.resize(length);
  if (scheme == "ecdsa-2p") return MAANY_MPC_SCHEME_ECDSA_2P;
  if (scheme == "ecdsa-tn") return MAANY_MPC_SCHEME_ECDSA_TN;
  if (scheme == "schnorr-2p") return MAANY_MPC_SCHEME_SCHNORR_2P;
  napi_throw_range_error(env, nullptr, "unsupported scheme value");
  return static_cast<maany_mpc_scheme_t>(-1);
}

std::vector<uint8_t> BufferToVector(napi_env env, napi_value value, const char* label) {
  bool is_buffer = false;
  napi_is_buffer(env, value, &is_buffer);
  if (!is_buffer) {
    std::string message = std::string(label) + " must be a Buffer";
    napi_throw_type_error(env, nullptr, message.c_str());
    return {};
  }
  void* data = nullptr;
  size_t length = 0;
  napi_get_buffer_info(env, value, &data, &length);
  std::vector<uint8_t> out(length);
  if (length) std::memcpy(out.data(), data, length);
  return out;
}

void SetBufferProp(napi_env env, napi_value obj, const char* name, const uint8_t* data, size_t len) {
  napi_value buffer;
  napi_create_buffer_copy(env, len, data, nullptr, &buffer);
  napi_set_named_property(env, obj, name, buffer);
}

napi_value JsDkgNew(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 2) {
    napi_throw_type_error(env, nullptr, "dkgNew expects (ctx, options)");
    return nullptr;
  }

  CtxHandle* ctx_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &ctx_handle)) return nullptr;
  if (!ctx_handle->ctx) {
    napi_throw_error(env, nullptr, "Context already shut down");
    return nullptr;
  }

  napi_value opts = argv[1];
  napi_valuetype type;
  napi_typeof(env, opts, &type);
  if (type != napi_object) {
    napi_throw_type_error(env, nullptr, "options must be an object");
    return nullptr;
  }

  maany_mpc_dkg_opts_t dkg_opts{};
  dkg_opts.curve = MAANY_MPC_CURVE_SECP256K1;
  dkg_opts.scheme = MAANY_MPC_SCHEME_ECDSA_2P;

  napi_value role_value;
  if (napi_get_named_property(env, opts, "role", &role_value) != napi_ok) {
    napi_throw_type_error(env, nullptr, "options.role is required");
    return nullptr;
  }
  maany_mpc_share_kind_t role = ParseRole(env, role_value);
  if (role != MAANY_MPC_SHARE_DEVICE && role != MAANY_MPC_SHARE_SERVER) return nullptr;
  dkg_opts.kind = role;

  napi_value key_id_value;
  if (napi_get_named_property(env, opts, "keyId", &key_id_value) == napi_ok) {
    napi_valuetype key_id_type;
    napi_typeof(env, key_id_value, &key_id_type);
    if (key_id_type != napi_undefined && key_id_type != napi_null) {
      bool is_buffer = false;
      napi_is_buffer(env, key_id_value, &is_buffer);
      if (!is_buffer) {
        napi_throw_type_error(env, nullptr, "keyId must be a Buffer");
        return nullptr;
      }
      void* data = nullptr;
      size_t length = 0;
      napi_get_buffer_info(env, key_id_value, &data, &length);
      if (length != sizeof(dkg_opts.key_id_hint.bytes)) {
        napi_throw_range_error(env, nullptr, "keyId must be 32 bytes");
        return nullptr;
      }
      std::memcpy(dkg_opts.key_id_hint.bytes, data, length);
    }
  }

  napi_value sid_value;
  if (napi_get_named_property(env, opts, "sessionId", &sid_value) == napi_ok) {
    napi_valuetype sid_type;
    napi_typeof(env, sid_value, &sid_type);
    if (sid_type != napi_undefined && sid_type != napi_null) {
      bool is_buffer = false;
      napi_is_buffer(env, sid_value, &is_buffer);
      if (!is_buffer) {
        napi_throw_type_error(env, nullptr, "sessionId must be a Buffer");
        return nullptr;
      }
      void* data = nullptr;
      size_t length = 0;
      napi_get_buffer_info(env, sid_value, &data, &length);
      dkg_opts.session_id.data = static_cast<uint8_t*>(data);
      dkg_opts.session_id.len = length;
    }
  }

  maany_mpc_dkg_t* dkg = nullptr;
  maany_mpc_error_t status = maany_mpc_dkg_new(ctx_handle->ctx, &dkg_opts, &dkg);
  if (status != MAANY_MPC_OK) {
    napi_throw(env, CreateError(env, "maany_mpc_dkg_new", status));
    return nullptr;
  }

  auto* handle = new DkgHandle{dkg};
  napi_value result = WrapHandle(env, handle, FinalizeDkg);
  if (!result) {
    FinalizeDkg(env, handle, nullptr);
    napi_throw_error(env, nullptr, "Failed to wrap DKG handle");
    return nullptr;
  }
  return result;
}

struct DkgStepWork : public DeferredWorkBase {
  CtxHandle* ctx_handle;
  DkgHandle* dkg_handle;
  std::vector<uint8_t> inbound;
  std::vector<uint8_t> outbound;
  maany_mpc_step_result_t step_result{MAANY_MPC_STEP_CONTINUE};
};

void DkgStepExecute(napi_env /*env*/, void* data) {
  auto* work = static_cast<DkgStepWork*>(data);
  if (!work->ctx_handle->ctx || !work->dkg_handle->dkg) {
    work->status = MAANY_MPC_ERR_PROTO_STATE;
    work->error_context = "dkgStep";
    return;
  }

  maany_mpc_buf_t inbound_buf{nullptr, 0};
  if (!work->inbound.empty()) {
    inbound_buf.data = work->inbound.data();
    inbound_buf.len = work->inbound.size();
  }

  maany_mpc_buf_t outbound_buf{nullptr, 0};
  maany_mpc_step_result_t result = MAANY_MPC_STEP_CONTINUE;
  work->status = maany_mpc_dkg_step(work->ctx_handle->ctx, work->dkg_handle->dkg,
                                    work->inbound.empty() ? nullptr : &inbound_buf, &outbound_buf, &result);
  work->step_result = result;
  if (work->status == MAANY_MPC_OK && outbound_buf.data && outbound_buf.len) {
    auto* bytes = static_cast<uint8_t*>(outbound_buf.data);
    work->outbound.assign(bytes, bytes + outbound_buf.len);
  }
  if (outbound_buf.data) {
    maany_mpc_buf_free(work->ctx_handle->ctx, &outbound_buf);
  }
  work->error_context = "maany_mpc_dkg_step";
}

void DkgStepComplete(napi_env env, napi_status status, void* data) {
  auto* work = static_cast<DkgStepWork*>(data);
  if (status != napi_ok) {
    napi_value err;
    napi_get_and_clear_last_exception(env, &err);
    napi_reject_deferred(env, work->deferred, err);
    napi_delete_async_work(env, work->work);
    delete work;
    return;
  }

  if (work->status != MAANY_MPC_OK) {
    napi_value err = CreateError(env, work->error_context, work->status);
    napi_reject_deferred(env, work->deferred, err);
    napi_delete_async_work(env, work->work);
    delete work;
    return;
  }

  napi_value result_obj;
  napi_create_object(env, &result_obj);

  napi_value done_value;
  napi_get_boolean(env, work->step_result == MAANY_MPC_STEP_DONE, &done_value);
  napi_set_named_property(env, result_obj, "done", done_value);

  if (!work->outbound.empty()) {
    napi_value buffer;
    void* dst = nullptr;
    napi_create_buffer(env, work->outbound.size(), &dst, &buffer);
    std::memcpy(dst, work->outbound.data(), work->outbound.size());
    napi_set_named_property(env, result_obj, "outMsg", buffer);
  }

  napi_resolve_deferred(env, work->deferred, result_obj);
  napi_delete_async_work(env, work->work);
  delete work;
}

napi_value JsDkgStep(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 2) {
    napi_throw_type_error(env, nullptr, "dkgStep expects (ctx, dkg, [inPeerMsg])");
    return nullptr;
  }

  CtxHandle* ctx_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &ctx_handle)) return nullptr;
  if (!ctx_handle->ctx) {
    napi_throw_error(env, nullptr, "Context already shut down");
    return nullptr;
  }

  DkgHandle* dkg_handle = nullptr;
  if (!UnwrapHandle(env, argv[1], &dkg_handle)) return nullptr;
  if (!dkg_handle->dkg) {
    napi_throw_error(env, nullptr, "DKG handle already finalized");
    return nullptr;
  }

  auto* work = new DkgStepWork();
  work->env = env;
  work->ctx_handle = ctx_handle;
  work->dkg_handle = dkg_handle;

  if (argc >= 3 && argv[2] != nullptr) {
    napi_valuetype type;
    napi_typeof(env, argv[2], &type);
    bool is_buffer = false;
    if (type != napi_undefined && type != napi_null) {
      napi_is_buffer(env, argv[2], &is_buffer);
      if (!is_buffer) {
        delete work;
        napi_throw_type_error(env, nullptr, "inPeerMsg must be a Buffer or undefined");
        return nullptr;
      }
      void* data = nullptr;
      size_t len = 0;
      napi_get_buffer_info(env, argv[2], &data, &len);
      if (len > 0 && data) {
        auto* bytes = static_cast<uint8_t*>(data);
        work->inbound.assign(bytes, bytes + len);
      }
    }
  }

  napi_value promise;
  napi_create_promise(env, &work->deferred, &promise);

  napi_value resource_name;
  napi_create_string_utf8(env, "dkgStep", NAPI_AUTO_LENGTH, &resource_name);
  napi_create_async_work(env, nullptr, resource_name, DkgStepExecute, DkgStepComplete, work, &work->work);
  napi_queue_async_work(env, work->work);

  return promise;
}

napi_value JsDkgFinalize(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 2) {
    napi_throw_type_error(env, nullptr, "dkgFinalize expects (ctx, dkg)");
    return nullptr;
  }

  CtxHandle* ctx_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &ctx_handle)) return nullptr;
  if (!ctx_handle->ctx) {
    napi_throw_error(env, nullptr, "Context already shut down");
    return nullptr;
  }

  DkgHandle* dkg_handle = nullptr;
  if (!UnwrapHandle(env, argv[1], &dkg_handle)) return nullptr;
  if (!dkg_handle->dkg) {
    napi_throw_error(env, nullptr, "DKG handle already finalized");
    return nullptr;
  }

  maany_mpc_keypair_t* kp = nullptr;
  maany_mpc_error_t status = maany_mpc_dkg_finalize(ctx_handle->ctx, dkg_handle->dkg, &kp);
  if (status != MAANY_MPC_OK) {
    napi_throw(env, CreateError(env, "maany_mpc_dkg_finalize", status));
    return nullptr;
  }

  maany_mpc_dkg_free(dkg_handle->dkg);
  dkg_handle->dkg = nullptr;

  auto* kp_handle = new KeypairHandle{kp};
  napi_value result = WrapHandle(env, kp_handle, FinalizeKeypair);
  if (!result) {
    FinalizeKeypair(env, kp_handle, nullptr);
    napi_throw_error(env, nullptr, "Failed to wrap keypair handle");
    return nullptr;
  }
  return result;
}

struct SignHandle {
  maany_mpc_sign_t* sign;
};

void FinalizeSign(napi_env /*env*/, void* data, void* /*hint*/) {
  auto* handle = static_cast<SignHandle*>(data);
  if (!handle) return;
  if (handle->sign) {
    maany_mpc_sign_free(handle->sign);
    handle->sign = nullptr;
  }
  delete handle;
}

napi_value JsSignFree(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "signFree expects a sign handle");
    return nullptr;
  }

  SignHandle* sign_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &sign_handle)) return nullptr;
  if (sign_handle->sign) {
    maany_mpc_sign_free(sign_handle->sign);
    sign_handle->sign = nullptr;
  }
  return nullptr;
}

napi_value JsSignNew(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 2) {
    napi_throw_type_error(env, nullptr, "signNew expects (ctx, keypair, [opts])");
    return nullptr;
  }

  CtxHandle* ctx_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &ctx_handle)) return nullptr;
  if (!ctx_handle->ctx) {
    napi_throw_error(env, nullptr, "Context already shut down");
    return nullptr;
  }

  KeypairHandle* kp_handle = nullptr;
  if (!UnwrapHandle(env, argv[1], &kp_handle)) return nullptr;
  if (!kp_handle->kp) {
    napi_throw_error(env, nullptr, "Keypair handle already freed");
    return nullptr;
  }

  maany_mpc_sign_opts_t opts{};
  opts.scheme = MAANY_MPC_SCHEME_ECDSA_2P;

  if (argc >= 3 && argv[2] != nullptr) {
    napi_valuetype opt_type;
    napi_typeof(env, argv[2], &opt_type);
    if (opt_type != napi_undefined && opt_type != napi_null) {
      if (opt_type != napi_object) {
        napi_throw_type_error(env, nullptr, "sign options must be an object");
        return nullptr;
      }
      napi_value sid_value;
      if (napi_get_named_property(env, argv[2], "sessionId", &sid_value) == napi_ok) {
        napi_valuetype sid_type;
        napi_typeof(env, sid_value, &sid_type);
        if (sid_type != napi_undefined && sid_type != napi_null) {
          bool is_buffer = false;
          napi_is_buffer(env, sid_value, &is_buffer);
          if (!is_buffer) {
            napi_throw_type_error(env, nullptr, "sessionId must be a Buffer");
            return nullptr;
          }
          void* data = nullptr;
          size_t len = 0;
          napi_get_buffer_info(env, sid_value, &data, &len);
          opts.session_id.data = static_cast<uint8_t*>(data);
          opts.session_id.len = len;
        }
      }
      napi_value aad_value;
      if (napi_get_named_property(env, argv[2], "extraAad", &aad_value) == napi_ok) {
        napi_valuetype aad_type;
        napi_typeof(env, aad_value, &aad_type);
        if (aad_type != napi_undefined && aad_type != napi_null) {
          bool is_buffer = false;
          napi_is_buffer(env, aad_value, &is_buffer);
          if (!is_buffer) {
            napi_throw_type_error(env, nullptr, "extraAad must be a Buffer");
            return nullptr;
          }
          void* data = nullptr;
          size_t len = 0;
          napi_get_buffer_info(env, aad_value, &data, &len);
          opts.extra_aad.data = static_cast<uint8_t*>(data);
          opts.extra_aad.len = len;
        }
      }
    }
  }

  maany_mpc_sign_t* sign = nullptr;
  maany_mpc_error_t status = maany_mpc_sign_new(ctx_handle->ctx, kp_handle->kp, &opts, &sign);
  if (status != MAANY_MPC_OK) {
    napi_throw(env, CreateError(env, "maany_mpc_sign_new", status));
    return nullptr;
  }

  auto* sign_handle = new SignHandle{sign};
  napi_value result = WrapHandle(env, sign_handle, FinalizeSign);
  if (!result) {
    FinalizeSign(env, sign_handle, nullptr);
    napi_throw_error(env, nullptr, "Failed to wrap sign handle");
    return nullptr;
  }
  return result;
}

napi_value JsSignSetMessage(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 3) {
    napi_throw_type_error(env, nullptr, "signSetMessage expects (ctx, sign, message)");
    return nullptr;
  }

  CtxHandle* ctx_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &ctx_handle)) return nullptr;
  if (!ctx_handle->ctx) {
    napi_throw_error(env, nullptr, "Context already shut down");
    return nullptr;
  }

  SignHandle* sign_handle = nullptr;
  if (!UnwrapHandle(env, argv[1], &sign_handle)) return nullptr;
  if (!sign_handle->sign) {
    napi_throw_error(env, nullptr, "Sign handle already freed");
    return nullptr;
  }

  bool is_buffer = false;
  napi_is_buffer(env, argv[2], &is_buffer);
  if (!is_buffer) {
    napi_throw_type_error(env, nullptr, "message must be a Buffer");
    return nullptr;
  }
  void* data = nullptr;
  size_t len = 0;
  napi_get_buffer_info(env, argv[2], &data, &len);
  if (len == 0) {
    napi_throw_range_error(env, nullptr, "message must not be empty");
    return nullptr;
  }

  maany_mpc_error_t status = maany_mpc_sign_set_message(ctx_handle->ctx, sign_handle->sign,
                                                        static_cast<uint8_t*>(data), len);
  if (status != MAANY_MPC_OK) {
    napi_throw(env, CreateError(env, "maany_mpc_sign_set_message", status));
    return nullptr;
  }
  return nullptr;
}

struct SignStepWork : public DeferredWorkBase {
  CtxHandle* ctx_handle;
  SignHandle* sign_handle;
  std::vector<uint8_t> inbound;
  std::vector<uint8_t> outbound;
  maany_mpc_step_result_t step_result{MAANY_MPC_STEP_CONTINUE};
};

void SignStepExecute(napi_env /*env*/, void* data) {
  auto* work = static_cast<SignStepWork*>(data);
  if (!work->ctx_handle->ctx || !work->sign_handle->sign) {
    work->status = MAANY_MPC_ERR_PROTO_STATE;
    work->error_context = "signStep";
    return;
  }

  maany_mpc_buf_t inbound_buf{nullptr, 0};
  if (!work->inbound.empty()) {
    inbound_buf.data = work->inbound.data();
    inbound_buf.len = work->inbound.size();
  }
  maany_mpc_buf_t outbound_buf{nullptr, 0};
  work->status = maany_mpc_sign_step(work->ctx_handle->ctx, work->sign_handle->sign,
                                     work->inbound.empty() ? nullptr : &inbound_buf, &outbound_buf,
                                     &work->step_result);
  if (work->status == MAANY_MPC_OK && outbound_buf.data && outbound_buf.len) {
    auto* bytes = static_cast<uint8_t*>(outbound_buf.data);
    work->outbound.assign(bytes, bytes + outbound_buf.len);
  }
  if (outbound_buf.data) {
    maany_mpc_buf_free(work->ctx_handle->ctx, &outbound_buf);
  }
  work->error_context = "maany_mpc_sign_step";
}

void SignStepComplete(napi_env env, napi_status status, void* data) {
  auto* work = static_cast<SignStepWork*>(data);
  if (status != napi_ok) {
    napi_value err;
    napi_get_and_clear_last_exception(env, &err);
    napi_reject_deferred(env, work->deferred, err);
    napi_delete_async_work(env, work->work);
    delete work;
    return;
  }

  if (work->status != MAANY_MPC_OK) {
    napi_value err = CreateError(env, work->error_context, work->status);
    napi_reject_deferred(env, work->deferred, err);
    napi_delete_async_work(env, work->work);
    delete work;
    return;
  }

  napi_value result_obj;
  napi_create_object(env, &result_obj);

  napi_value done_value;
  napi_get_boolean(env, work->step_result == MAANY_MPC_STEP_DONE, &done_value);
  napi_set_named_property(env, result_obj, "done", done_value);

  if (!work->outbound.empty()) {
    napi_value buffer;
    void* dst = nullptr;
    napi_create_buffer(env, work->outbound.size(), &dst, &buffer);
    std::memcpy(dst, work->outbound.data(), work->outbound.size());
    napi_set_named_property(env, result_obj, "outMsg", buffer);
  }

  napi_resolve_deferred(env, work->deferred, result_obj);
  napi_delete_async_work(env, work->work);
  delete work;
}

napi_value JsSignStep(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 2) {
    napi_throw_type_error(env, nullptr, "signStep expects (ctx, sign, [inPeerMsg])");
    return nullptr;
  }

  CtxHandle* ctx_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &ctx_handle)) return nullptr;
  if (!ctx_handle->ctx) {
    napi_throw_error(env, nullptr, "Context already shut down");
    return nullptr;
  }

  SignHandle* sign_handle = nullptr;
  if (!UnwrapHandle(env, argv[1], &sign_handle)) return nullptr;
  if (!sign_handle->sign) {
    napi_throw_error(env, nullptr, "Sign handle already freed");
    return nullptr;
  }

  auto* work = new SignStepWork();
  work->env = env;
  work->ctx_handle = ctx_handle;
  work->sign_handle = sign_handle;

  if (argc >= 3 && argv[2] != nullptr) {
    bool is_buffer = false;
    napi_valuetype type;
    napi_typeof(env, argv[2], &type);
    if (type != napi_undefined && type != napi_null) {
      napi_is_buffer(env, argv[2], &is_buffer);
      if (!is_buffer) {
        delete work;
        napi_throw_type_error(env, nullptr, "inPeerMsg must be a Buffer or undefined");
        return nullptr;
      }
      void* data = nullptr;
      size_t len = 0;
      napi_get_buffer_info(env, argv[2], &data, &len);
      if (len > 0 && data) {
        auto* bytes = static_cast<uint8_t*>(data);
        work->inbound.assign(bytes, bytes + len);
      }
    }
  }

  napi_value promise;
  napi_create_promise(env, &work->deferred, &promise);

  napi_value resource_name;
  napi_create_string_utf8(env, "signStep", NAPI_AUTO_LENGTH, &resource_name);
  napi_create_async_work(env, nullptr, resource_name, SignStepExecute, SignStepComplete, work, &work->work);
  napi_queue_async_work(env, work->work);

  return promise;
}

napi_value JsSignFinalize(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 2) {
    napi_throw_type_error(env, nullptr, "signFinalize expects (ctx, sign, [format])");
    return nullptr;
  }

  CtxHandle* ctx_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &ctx_handle)) return nullptr;
  if (!ctx_handle->ctx) {
    napi_throw_error(env, nullptr, "Context already shut down");
    return nullptr;
  }

  SignHandle* sign_handle = nullptr;
  if (!UnwrapHandle(env, argv[1], &sign_handle)) return nullptr;
  if (!sign_handle->sign) {
    napi_throw_error(env, nullptr, "Sign handle already freed");
    return nullptr;
  }

  maany_mpc_sig_format_t format = MAANY_MPC_SIG_FORMAT_DER;
  if (argc >= 3 && argv[2] != nullptr) {
    napi_valuetype type;
    napi_typeof(env, argv[2], &type);
    if (type == napi_string) {
      size_t len = 0;
      napi_get_value_string_utf8(env, argv[2], nullptr, 0, &len);
      std::string fmt(len, '\0');
      napi_get_value_string_utf8(env, argv[2], fmt.data(), fmt.size() + 1, &len);
      fmt.resize(len);
      if (fmt == "der") {
        format = MAANY_MPC_SIG_FORMAT_DER;
      } else if (fmt == "raw-rs") {
        format = MAANY_MPC_SIG_FORMAT_RAW_RS;
      } else {
        napi_throw_range_error(env, nullptr, "format must be 'der' or 'raw-rs'");
        return nullptr;
      }
    }
  }

  maany_mpc_buf_t sig{nullptr, 0};
  maany_mpc_error_t status = maany_mpc_sign_finalize(ctx_handle->ctx, sign_handle->sign, format, &sig);
  if (status != MAANY_MPC_OK) {
    napi_throw(env, CreateError(env, "maany_mpc_sign_finalize", status));
    return nullptr;
  }

  napi_value buffer;
  void* dst = nullptr;
  napi_create_buffer(env, sig.len, &dst, &buffer);
  std::memcpy(dst, sig.data, sig.len);
  maany_mpc_buf_free(ctx_handle->ctx, &sig);
  return buffer;
}

napi_value JsRefreshNew(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 2) {
    napi_throw_type_error(env, nullptr, "refreshNew expects (ctx, keypair, [opts])");
    return nullptr;
  }

  CtxHandle* ctx_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &ctx_handle)) return nullptr;
  if (!ctx_handle->ctx) {
    napi_throw_error(env, nullptr, "Context already shut down");
    return nullptr;
  }

  KeypairHandle* kp_handle = nullptr;
  if (!UnwrapHandle(env, argv[1], &kp_handle)) return nullptr;
  if (!kp_handle->kp) {
    napi_throw_error(env, nullptr, "Keypair handle already freed");
    return nullptr;
  }

  maany_mpc_refresh_opts_t opts{};
  if (argc >= 3 && argv[2] != nullptr) {
    napi_valuetype opt_type;
    napi_typeof(env, argv[2], &opt_type);
    if (opt_type != napi_undefined && opt_type != napi_null) {
      if (opt_type != napi_object) {
        napi_throw_type_error(env, nullptr, "refresh options must be an object");
        return nullptr;
      }
      napi_value sid_value;
      if (napi_get_named_property(env, argv[2], "sessionId", &sid_value) == napi_ok) {
        bool is_buffer = false;
        napi_is_buffer(env, sid_value, &is_buffer);
        if (!is_buffer) {
          napi_throw_type_error(env, nullptr, "sessionId must be a Buffer");
          return nullptr;
        }
        void* data = nullptr;
        size_t len = 0;
        napi_get_buffer_info(env, sid_value, &data, &len);
        opts.session_id.data = static_cast<uint8_t*>(data);
        opts.session_id.len = len;
      }
    }
  }

  maany_mpc_dkg_t* refresher = nullptr;
  maany_mpc_error_t status = maany_mpc_refresh_new(ctx_handle->ctx, kp_handle->kp, &opts, &refresher);
  if (status != MAANY_MPC_OK) {
    napi_throw(env, CreateError(env, "maany_mpc_refresh_new", status));
    return nullptr;
  }

  auto* handle = new DkgHandle{refresher};
  napi_value result = WrapHandle(env, handle, FinalizeDkg);
  if (!result) {
    FinalizeDkg(env, handle, nullptr);
    napi_throw_error(env, nullptr, "Failed to wrap refresh handle");
    return nullptr;
  }
  return result;
}

napi_value JsBackupCreate(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 2) {
    napi_throw_type_error(env, nullptr, "backupCreate expects (ctx, keypair, [options])");
    return nullptr;
  }

  CtxHandle* ctx_handle = nullptr;
  KeypairHandle* kp_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &ctx_handle) || !UnwrapHandle(env, argv[1], &kp_handle)) return nullptr;
  if (!ctx_handle->ctx || !kp_handle->kp) {
    napi_throw_error(env, nullptr, "Context or keypair handle invalid");
    return nullptr;
  }

  uint32_t threshold = 2;
  size_t share_count = 3;
  std::vector<uint8_t> label_vec;

  if (argc >= 3 && argv[2] != nullptr) {
    napi_valuetype type;
    napi_typeof(env, argv[2], &type);
    if (type != napi_undefined && type != napi_null) {
      if (type != napi_object) {
        napi_throw_type_error(env, nullptr, "options must be an object");
        return nullptr;
      }
      napi_value opts = argv[2];

      napi_value threshold_value;
      if (napi_get_named_property(env, opts, "threshold", &threshold_value) == napi_ok) {
        double temp = 0;
        napi_get_value_double(env, threshold_value, &temp);
        if (temp < 1) {
          napi_throw_range_error(env, nullptr, "threshold must be >= 1");
          return nullptr;
        }
        threshold = static_cast<uint32_t>(temp);
      }

      napi_value shares_value;
      if (napi_get_named_property(env, opts, "shareCount", &shares_value) == napi_ok ||
          napi_get_named_property(env, opts, "shares", &shares_value) == napi_ok) {
        double temp = 0;
        napi_get_value_double(env, shares_value, &temp);
        if (temp < 1) {
          napi_throw_range_error(env, nullptr, "shareCount must be >= 1");
          return nullptr;
        }
        share_count = static_cast<size_t>(temp);
      }

      napi_value label_value;
      if (napi_get_named_property(env, opts, "label", &label_value) == napi_ok) {
        napi_valuetype label_type;
        napi_typeof(env, label_value, &label_type);
        if (label_type != napi_undefined && label_type != napi_null) {
          label_vec = BufferToVector(env, label_value, "label");
        }
      }
    }
  }

  if (share_count < threshold) {
    napi_throw_range_error(env, nullptr, "shareCount must be >= threshold");
    return nullptr;
  }

  maany_mpc_buf_t label_buf{nullptr, 0};
  if (!label_vec.empty()) {
    label_buf.data = label_vec.data();
    label_buf.len = label_vec.size();
  }

  maany_mpc_backup_ciphertext_t cipher{};
  std::vector<maany_mpc_backup_share_t> share_structs(share_count);
  maany_mpc_error_t status = maany_mpc_backup_create(
    ctx_handle->ctx,
    kp_handle->kp,
    threshold,
    share_count,
    label_vec.empty() ? nullptr : &label_buf,
    &cipher,
    share_structs.data());
  if (status != MAANY_MPC_OK) {
    napi_throw(env, CreateError(env, "maany_mpc_backup_create", status));
    return nullptr;
  }

  napi_value ciphertext_obj;
  napi_create_object(env, &ciphertext_obj);

  napi_value kind_value;
  napi_create_string_utf8(env, ShareKindToString(cipher.kind), NAPI_AUTO_LENGTH, &kind_value);
  napi_set_named_property(env, ciphertext_obj, "kind", kind_value);

  napi_value curve_value;
  napi_create_string_utf8(env, CurveToString(cipher.curve), NAPI_AUTO_LENGTH, &curve_value);
  napi_set_named_property(env, ciphertext_obj, "curve", curve_value);

  napi_value scheme_value;
  napi_create_string_utf8(env, SchemeToString(cipher.scheme), NAPI_AUTO_LENGTH, &scheme_value);
  napi_set_named_property(env, ciphertext_obj, "scheme", scheme_value);

  napi_value threshold_value;
  napi_create_uint32(env, cipher.threshold, &threshold_value);
  napi_set_named_property(env, ciphertext_obj, "threshold", threshold_value);

  napi_value share_count_value;
  napi_create_uint32(env, cipher.share_count, &share_count_value);
  napi_set_named_property(env, ciphertext_obj, "shareCount", share_count_value);

  SetBufferProp(env, ciphertext_obj, "keyId", cipher.key_id.bytes, sizeof(cipher.key_id.bytes));
  if (cipher.label.data && cipher.label.len) {
    SetBufferProp(env, ciphertext_obj, "label", static_cast<uint8_t*>(cipher.label.data), cipher.label.len);
  } else {
    napi_value empty;
    napi_create_buffer(env, 0, nullptr, &empty);
    napi_set_named_property(env, ciphertext_obj, "label", empty);
  }
  if (cipher.ciphertext.data && cipher.ciphertext.len) {
    SetBufferProp(env, ciphertext_obj, "blob", static_cast<uint8_t*>(cipher.ciphertext.data), cipher.ciphertext.len);
  } else {
    napi_value empty;
    napi_create_buffer(env, 0, nullptr, &empty);
    napi_set_named_property(env, ciphertext_obj, "blob", empty);
  }

  maany_mpc_buf_free(ctx_handle->ctx, &cipher.label);
  maany_mpc_buf_free(ctx_handle->ctx, &cipher.ciphertext);

  napi_value shares_array;
  napi_create_array_with_length(env, share_count, &shares_array);
  for (size_t i = 0; i < share_count; ++i) {
    napi_value share_buffer;
    if (share_structs[i].data.data && share_structs[i].data.len) {
      napi_create_buffer_copy(env, share_structs[i].data.len, share_structs[i].data.data, nullptr, &share_buffer);
    } else {
      napi_create_buffer(env, 0, nullptr, &share_buffer);
    }
    napi_set_element(env, shares_array, i, share_buffer);
    maany_mpc_buf_free(ctx_handle->ctx, &share_structs[i].data);
  }

  napi_value result;
  napi_create_object(env, &result);
  napi_set_named_property(env, result, "ciphertext", ciphertext_obj);
  napi_set_named_property(env, result, "shares", shares_array);
  return result;
}

napi_value JsBackupRestore(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 3) {
    napi_throw_type_error(env, nullptr, "backupRestore expects (ctx, ciphertext, shares)");
    return nullptr;
  }

  CtxHandle* ctx_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &ctx_handle)) return nullptr;
  if (!ctx_handle->ctx) {
    napi_throw_error(env, nullptr, "Context already shut down");
    return nullptr;
  }

  napi_value cipher_obj = argv[1];
  napi_valuetype cipher_type;
  napi_typeof(env, cipher_obj, &cipher_type);
  if (cipher_type != napi_object) {
    napi_throw_type_error(env, nullptr, "ciphertext must be an object");
    return nullptr;
  }

  maany_mpc_backup_ciphertext_t cipher{};

  napi_value kind_value;
  napi_get_named_property(env, cipher_obj, "kind", &kind_value);
  cipher.kind = ShareKindFromString(env, kind_value);

  napi_value curve_value;
  napi_get_named_property(env, cipher_obj, "curve", &curve_value);
  cipher.curve = CurveFromString(env, curve_value);

  napi_value scheme_value;
  napi_get_named_property(env, cipher_obj, "scheme", &scheme_value);
  cipher.scheme = SchemeFromString(env, scheme_value);

  napi_value threshold_value;
  napi_get_named_property(env, cipher_obj, "threshold", &threshold_value);
  napi_get_value_uint32(env, threshold_value, &cipher.threshold);

  napi_value share_count_value;
  napi_get_named_property(env, cipher_obj, "shareCount", &share_count_value);
  napi_get_value_uint32(env, share_count_value, &cipher.share_count);

  napi_value key_id_value;
  napi_get_named_property(env, cipher_obj, "keyId", &key_id_value);
  std::vector<uint8_t> key_id_vec = BufferToVector(env, key_id_value, "keyId");
  if (key_id_vec.size() != sizeof(cipher.key_id.bytes)) {
    napi_throw_range_error(env, nullptr, "keyId must be 32 bytes");
    return nullptr;
  }
  std::memcpy(cipher.key_id.bytes, key_id_vec.data(), key_id_vec.size());

  std::vector<uint8_t> label_vec;
  napi_value label_value;
  if (napi_get_named_property(env, cipher_obj, "label", &label_value) == napi_ok) {
    napi_valuetype label_type;
    napi_typeof(env, label_value, &label_type);
    if (label_type != napi_undefined && label_type != napi_null) {
      label_vec = BufferToVector(env, label_value, "label");
    }
  }
  if (!label_vec.empty()) {
    cipher.label.data = label_vec.data();
    cipher.label.len = label_vec.size();
  }

  napi_value blob_value;
  napi_get_named_property(env, cipher_obj, "blob", &blob_value);
  std::vector<uint8_t> blob_vec = BufferToVector(env, blob_value, "blob");
  if (blob_vec.empty()) {
    napi_throw_range_error(env, nullptr, "ciphertext blob must not be empty");
    return nullptr;
  }
  cipher.ciphertext.data = blob_vec.data();
  cipher.ciphertext.len = blob_vec.size();

  bool is_array = false;
  napi_is_array(env, argv[2], &is_array);
  if (!is_array) {
    napi_throw_type_error(env, nullptr, "shares must be an array");
    return nullptr;
  }
  uint32_t share_len = 0;
  napi_get_array_length(env, argv[2], &share_len);
  if (share_len < cipher.threshold) {
    napi_throw_range_error(env, nullptr, "insufficient shares provided");
    return nullptr;
  }

  std::vector<std::vector<uint8_t>> share_storage(share_len);
  std::vector<maany_mpc_backup_share_t> share_structs(share_len);
  for (uint32_t i = 0; i < share_len; ++i) {
    napi_value share_value;
    napi_get_element(env, argv[2], i, &share_value);
    share_storage[i] = BufferToVector(env, share_value, "share");
    if (share_storage[i].empty()) {
      napi_throw_range_error(env, nullptr, "share must not be empty");
      return nullptr;
    }
    share_structs[i].data.data = share_storage[i].data();
    share_structs[i].data.len = share_storage[i].size();
  }

  maany_mpc_keypair_t* restored = nullptr;
  maany_mpc_error_t status = maany_mpc_backup_restore(
    ctx_handle->ctx,
    &cipher,
    share_structs.data(),
    share_structs.size(),
    &restored);
  if (status != MAANY_MPC_OK) {
    napi_throw(env, CreateError(env, "maany_mpc_backup_restore", status));
    return nullptr;
  }

  auto* handle = new KeypairHandle{restored};
  napi_value result = WrapHandle(env, handle, FinalizeKeypair);
  if (!result) {
    FinalizeKeypair(env, handle, nullptr);
    napi_throw_error(env, nullptr, "Failed to wrap restored keypair");
    return nullptr;
  }
  return result;
}

napi_value JsKpExport(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 2) {
    napi_throw_type_error(env, nullptr, "kpExport expects (ctx, keypair)");
    return nullptr;
  }

  CtxHandle* ctx_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &ctx_handle)) return nullptr;
  if (!ctx_handle->ctx) {
    napi_throw_error(env, nullptr, "Context already shut down");
    return nullptr;
  }

  KeypairHandle* kp_handle = nullptr;
  if (!UnwrapHandle(env, argv[1], &kp_handle)) return nullptr;
  if (!kp_handle->kp) {
    napi_throw_error(env, nullptr, "Keypair handle already freed");
    return nullptr;
  }

  maany_mpc_buf_t blob{nullptr, 0};
  maany_mpc_error_t status = maany_mpc_kp_export(ctx_handle->ctx, kp_handle->kp, &blob);
  if (status != MAANY_MPC_OK) {
    napi_throw(env, CreateError(env, "maany_mpc_kp_export", status));
    return nullptr;
  }

  napi_value buffer;
  void* dst = nullptr;
  napi_create_buffer(env, blob.len, &dst, &buffer);
  std::memcpy(dst, blob.data, blob.len);
  maany_mpc_buf_free(ctx_handle->ctx, &blob);
  return buffer;
}

napi_value JsKpImport(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 2) {
    napi_throw_type_error(env, nullptr, "kpImport expects (ctx, blob)");
    return nullptr;
  }

  CtxHandle* ctx_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &ctx_handle)) return nullptr;
  if (!ctx_handle->ctx) {
    napi_throw_error(env, nullptr, "Context already shut down");
    return nullptr;
  }

  bool is_buffer = false;
  napi_is_buffer(env, argv[1], &is_buffer);
  if (!is_buffer) {
    napi_throw_type_error(env, nullptr, "blob must be a Buffer");
    return nullptr;
  }
  void* data = nullptr;
  size_t len = 0;
  napi_get_buffer_info(env, argv[1], &data, &len);

  maany_mpc_buf_t blob{static_cast<uint8_t*>(data), len};
  maany_mpc_keypair_t* kp = nullptr;
  maany_mpc_error_t status = maany_mpc_kp_import(ctx_handle->ctx, &blob, &kp);
  if (status != MAANY_MPC_OK) {
    napi_throw(env, CreateError(env, "maany_mpc_kp_import", status));
    return nullptr;
  }

  auto* kp_handle = new KeypairHandle{kp};
  napi_value result = WrapHandle(env, kp_handle, FinalizeKeypair);
  if (!result) {
    FinalizeKeypair(env, kp_handle, nullptr);
    napi_throw_error(env, nullptr, "Failed to wrap keypair handle");
    return nullptr;
  }
  return result;
}

napi_value JsKpPubkey(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 2) {
    napi_throw_type_error(env, nullptr, "kpPubkey expects (ctx, keypair)");
    return nullptr;
  }

  CtxHandle* ctx_handle = nullptr;
  if (!UnwrapHandle(env, argv[0], &ctx_handle)) return nullptr;
  if (!ctx_handle->ctx) {
    napi_throw_error(env, nullptr, "Context already shut down");
    return nullptr;
  }

  KeypairHandle* kp_handle = nullptr;
  if (!UnwrapHandle(env, argv[1], &kp_handle)) return nullptr;
  if (!kp_handle->kp) {
    napi_throw_error(env, nullptr, "Keypair handle already freed");
    return nullptr;
  }

  maany_mpc_buf_t pub_buf{nullptr, 0};
  maany_mpc_pubkey_t pubkey{};
  pubkey.pubkey = pub_buf;
  maany_mpc_error_t status = maany_mpc_kp_pubkey(ctx_handle->ctx, kp_handle->kp, &pubkey);
  if (status != MAANY_MPC_OK) {
    napi_throw(env, CreateError(env, "maany_mpc_kp_pubkey", status));
    return nullptr;
  }

  napi_value buffer;
  void* dst = nullptr;
  napi_create_buffer(env, pubkey.pubkey.len, &dst, &buffer);
  std::memcpy(dst, pubkey.pubkey.data, pubkey.pubkey.len);
  maany_mpc_buf_free(ctx_handle->ctx, &pubkey.pubkey);

  napi_value result;
  napi_create_object(env, &result);

  napi_value curve_value;
  napi_create_uint32(env, static_cast<uint32_t>(pubkey.curve), &curve_value);
  napi_set_named_property(env, result, "curve", curve_value);
  napi_set_named_property(env, result, "compressed", buffer);
  return result;
}

napi_value InitModule(napi_env env, napi_value exports) {
  napi_property_descriptor descriptors[] = {
      {"init", nullptr, JsInit, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"shutdown", nullptr, JsShutdown, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"dkgNew", nullptr, JsDkgNew, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"dkgStep", nullptr, JsDkgStep, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"dkgFinalize", nullptr, JsDkgFinalize, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"dkgFree", nullptr, JsDkgFree, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"kpExport", nullptr, JsKpExport, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"kpImport", nullptr, JsKpImport, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"kpPubkey", nullptr, JsKpPubkey, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"kpFree", nullptr, JsKpFree, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"signNew", nullptr, JsSignNew, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"signSetMessage", nullptr, JsSignSetMessage, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"signStep", nullptr, JsSignStep, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"signFinalize", nullptr, JsSignFinalize, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"signFree", nullptr, JsSignFree, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"refreshNew", nullptr, JsRefreshNew, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"backupCreate", nullptr, JsBackupCreate, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"backupRestore", nullptr, JsBackupRestore, nullptr, nullptr, nullptr, napi_default, nullptr},
  };

  napi_status status = napi_define_properties(env, exports, sizeof(descriptors) / sizeof(descriptors[0]), descriptors);
  if (status != napi_ok) {
    napi_throw_error(env, nullptr, "Failed to define exports");
    return nullptr;
  }
  return exports;
}

}  // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, InitModule)
