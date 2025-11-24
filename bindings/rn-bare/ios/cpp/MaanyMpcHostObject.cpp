#ifndef MAANY_MPC_HOST_OBJECT_IMPL
#define MAANY_MPC_HOST_OBJECT_IMPL

#include "MaanyMpcHostObject.h"

#include <jsi/jsi.h>

#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "include/maany_mpc.h"

namespace maany::rn {

namespace {

using facebook::jsi::Array;
using facebook::jsi::Function;
using facebook::jsi::HostObject;
using facebook::jsi::JSError;
using facebook::jsi::Object;
using facebook::jsi::PropNameID;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;

constexpr char kBindingGlobalName[] = "__maanyMpc";

void throwTypeError(Runtime& runtime, const std::string& message) {
  throw JSError(runtime, message);
}

[[noreturn]] void throwMaanyError(Runtime& runtime, const std::string& context, maany_mpc_error_t error) {
  const char* err = maany_mpc_error_string(error);
  std::string message = context;
  message.append(": ");
  message.append(err ? err : "unknown");
  throw JSError(runtime, message);
}

class CtxHostObject final : public HostObject {
 public:
  explicit CtxHostObject(maany_mpc_ctx_t* ctx) : ctx_(ctx) {}
  ~CtxHostObject() override {
    if (ctx_) {
      maany_mpc_shutdown(ctx_);
      ctx_ = nullptr;
    }
  }

  maany_mpc_ctx_t* ptr(Runtime& runtime) const {
    if (!ctx_) {
      throw JSError(runtime, "Context already shut down");
    }
    return ctx_;
  }

  void shutdown() {
    if (ctx_) {
      maany_mpc_shutdown(ctx_);
      ctx_ = nullptr;
    }
  }

 private:
  mutable maany_mpc_ctx_t* ctx_;
};

class DkgHostObject final : public HostObject {
 public:
  explicit DkgHostObject(maany_mpc_dkg_t* dkg) : dkg_(dkg) {}
  ~DkgHostObject() override {
    if (dkg_) {
      maany_mpc_dkg_free(dkg_);
      dkg_ = nullptr;
    }
  }

  maany_mpc_dkg_t* ptr(Runtime& runtime) const {
    if (!dkg_) {
      throw JSError(runtime, "DKG handle already finalized");
    }
    return dkg_;
  }

  void free() {
    if (dkg_) {
      maany_mpc_dkg_free(dkg_);
      dkg_ = nullptr;
    }
  }

  void release() { dkg_ = nullptr; }

 private:
  mutable maany_mpc_dkg_t* dkg_;
};

class KeypairHostObject final : public HostObject {
 public:
  explicit KeypairHostObject(maany_mpc_keypair_t* kp) : kp_(kp) {}
  ~KeypairHostObject() override {
    if (kp_) {
      maany_mpc_kp_free(kp_);
      kp_ = nullptr;
    }
  }

  maany_mpc_keypair_t* ptr(Runtime& runtime) const {
    if (!kp_) {
      throw JSError(runtime, "Keypair handle already freed");
    }
    return kp_;
  }

  void free() {
    if (kp_) {
      maany_mpc_kp_free(kp_);
      kp_ = nullptr;
    }
  }

 private:
  mutable maany_mpc_keypair_t* kp_;
};

class SignHostObject final : public HostObject {
 public:
  explicit SignHostObject(maany_mpc_sign_t* sign) : sign_(sign) {}
  ~SignHostObject() override {
    if (sign_) {
      maany_mpc_sign_free(sign_);
      sign_ = nullptr;
    }
  }

  maany_mpc_sign_t* ptr(Runtime& runtime) const {
    if (!sign_) {
      throw JSError(runtime, "Sign handle already freed");
    }
    return sign_;
  }

  void free() {
    if (sign_) {
      maany_mpc_sign_free(sign_);
      sign_ = nullptr;
    }
  }

 private:
  mutable maany_mpc_sign_t* sign_;
};

std::shared_ptr<CtxHostObject> requireCtx(Runtime& runtime, const Value& value) {
  if (!value.isObject()) {
    throwTypeError(runtime, "Expected context handle");
  }
  auto obj = value.getObject(runtime);
  auto ctx = obj.getHostObject<CtxHostObject>(runtime);
  if (!ctx) {
    throwTypeError(runtime, "Invalid context handle");
  }
  return ctx;
}

std::shared_ptr<DkgHostObject> requireDkg(Runtime& runtime, const Value& value) {
  if (!value.isObject()) {
    throwTypeError(runtime, "Expected DKG handle");
  }
  auto obj = value.getObject(runtime);
  auto dkg = obj.getHostObject<DkgHostObject>(runtime);
  if (!dkg) {
    throwTypeError(runtime, "Invalid DKG handle");
  }
  return dkg;
}

std::shared_ptr<KeypairHostObject> requireKeypair(Runtime& runtime, const Value& value) {
  if (!value.isObject()) {
    throwTypeError(runtime, "Expected keypair handle");
  }
  auto obj = value.getObject(runtime);
  auto kp = obj.getHostObject<KeypairHostObject>(runtime);
  if (!kp) {
    throwTypeError(runtime, "Invalid keypair handle");
  }
  return kp;
}

std::shared_ptr<SignHostObject> requireSign(Runtime& runtime, const Value& value) {
  if (!value.isObject()) {
    throwTypeError(runtime, "Expected sign handle");
  }
  auto obj = value.getObject(runtime);
  auto sign = obj.getHostObject<SignHostObject>(runtime);
  if (!sign) {
    throwTypeError(runtime, "Invalid sign handle");
  }
  return sign;
}

std::optional<Object> getOptionalObject(Runtime& runtime, const Object& source, const char* name) {
  if (!source.hasProperty(runtime, name)) {
    return std::nullopt;
  }
  auto value = source.getProperty(runtime, name);
  if (value.isUndefined() || value.isNull()) {
    return std::nullopt;
  }
  if (!value.isObject()) {
    throwTypeError(runtime, std::string(name) + " must be an object");
  }
  return value.getObject(runtime);
}

std::optional<Value> getOptionalProperty(Runtime& runtime, const Object& source, const char* name) {
  if (!source.hasProperty(runtime, name)) {
    return std::nullopt;
  }
  auto value = source.getProperty(runtime, name);
  if (value.isUndefined() || value.isNull()) {
    return std::nullopt;
  }
  return value;
}

std::string requireString(Runtime& runtime, const Value& value, const char* label) {
  if (!value.isString()) {
    throwTypeError(runtime, std::string(label) + " must be a string");
  }
  auto str = value.getString(runtime);
  return str.utf8(runtime);
}

String makeString(Runtime& runtime, const char* text) {
  return String::createFromUtf8(runtime, text);
}

const char* shareKindToString(maany_mpc_share_kind_t kind) {
  switch (kind) {
    case MAANY_MPC_SHARE_DEVICE:
      return "device";
    case MAANY_MPC_SHARE_SERVER:
      return "server";
    default:
      return "unknown";
  }
}

maany_mpc_share_kind_t parseShareKind(Runtime& runtime, const Value& value) {
  auto str = requireString(runtime, value, "kind");
  if (str == "device") return MAANY_MPC_SHARE_DEVICE;
  if (str == "server") return MAANY_MPC_SHARE_SERVER;
  throwTypeError(runtime, "Unsupported share kind: " + str);
  return MAANY_MPC_SHARE_DEVICE;
}

const char* curveToString(maany_mpc_curve_t curve) {
  switch (curve) {
    case MAANY_MPC_CURVE_SECP256K1:
      return "secp256k1";
    case MAANY_MPC_CURVE_ED25519:
      return "ed25519";
    default:
      return "unknown";
  }
}

maany_mpc_curve_t parseCurve(Runtime& runtime, const Value& value) {
  auto str = requireString(runtime, value, "curve");
  if (str == "secp256k1") return MAANY_MPC_CURVE_SECP256K1;
  if (str == "ed25519") return MAANY_MPC_CURVE_ED25519;
  throwTypeError(runtime, "Unsupported curve: " + str);
  return MAANY_MPC_CURVE_SECP256K1;
}

const char* schemeToString(maany_mpc_scheme_t scheme) {
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

maany_mpc_scheme_t parseScheme(Runtime& runtime, const Value& value) {
  auto str = requireString(runtime, value, "scheme");
  if (str == "ecdsa-2p") return MAANY_MPC_SCHEME_ECDSA_2P;
  if (str == "ecdsa-tn") return MAANY_MPC_SCHEME_ECDSA_TN;
  if (str == "schnorr-2p") return MAANY_MPC_SCHEME_SCHNORR_2P;
  throwTypeError(runtime, "Unsupported scheme: " + str);
  return MAANY_MPC_SCHEME_ECDSA_2P;
}

std::vector<uint8_t> toByteVector(Runtime& runtime, const Value& value, const char* label) {
  if (!value.isObject()) {
    throwTypeError(runtime, std::string(label) + " must be a Uint8Array or ArrayBuffer");
  }
  auto object = value.getObject(runtime);
  if (object.isArrayBuffer(runtime)) {
    auto buffer = object.getArrayBuffer(runtime);
    size_t length = buffer.size(runtime);
    std::vector<uint8_t> out(length);
    if (length > 0) {
      std::memcpy(out.data(), buffer.data(runtime), length);
    }
    return out;
  }

  if (object.hasProperty(runtime, "buffer")) {
    auto bufferValue = object.getProperty(runtime, "buffer");
    if (!bufferValue.isObject()) {
      throwTypeError(runtime, std::string(label) + " must be a Uint8Array");
    }
    auto bufferObj = bufferValue.getObject(runtime);
    if (!bufferObj.isArrayBuffer(runtime)) {
      throwTypeError(runtime, std::string(label) + " must be a Uint8Array");
    }
    auto arrayBuffer = bufferObj.getArrayBuffer(runtime);
    size_t byteOffset = 0;
    size_t byteLength = arrayBuffer.size(runtime);
    if (object.hasProperty(runtime, "byteOffset")) {
      byteOffset = static_cast<size_t>(object.getProperty(runtime, "byteOffset").asNumber());
    }
    if (object.hasProperty(runtime, "byteLength")) {
      byteLength = static_cast<size_t>(object.getProperty(runtime, "byteLength").asNumber());
    }
    if (byteOffset + byteLength > arrayBuffer.size(runtime)) {
      throwTypeError(runtime, std::string(label) + " has invalid view bounds");
    }
    std::vector<uint8_t> out(byteLength);
    if (byteLength > 0) {
      std::memcpy(out.data(), arrayBuffer.data(runtime) + byteOffset, byteLength);
    }
    return out;
  }

  throwTypeError(runtime, std::string(label) + " must be a Uint8Array or ArrayBuffer");
  return {};
}

Value makeUint8Array(Runtime& runtime, const std::vector<uint8_t>& bytes) {
  auto arrayBufferCtor = runtime.global().getPropertyAsFunction(runtime, "ArrayBuffer");
  auto arrayBufferValue = arrayBufferCtor.callAsConstructor(runtime, Value(static_cast<double>(bytes.size())));
  auto arrayBufferObj = arrayBufferValue.getObject(runtime);
  auto arrayBuffer = arrayBufferObj.getArrayBuffer(runtime);
  if (!bytes.empty()) {
    std::memcpy(arrayBuffer.data(runtime), bytes.data(), bytes.size());
  }
  auto uint8Ctor = runtime.global().getPropertyAsFunction(runtime, "Uint8Array");
  return uint8Ctor.callAsConstructor(runtime, arrayBufferValue);
}

Value makeResolvedPromise(Runtime& runtime, std::function<Value(Runtime&)> producer) {
  auto promiseCtor = runtime.global().getPropertyAsFunction(runtime, "Promise");
  auto executor = Function::createFromHostFunction(
      runtime, PropNameID::forAscii(runtime, "maanyMpcExecutor"), 2,
      [producer = std::move(producer)](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
        if (count < 2 || !args[0].isObject() || !args[1].isObject()) {
          throwTypeError(rt, "Promise executor expects resolve/reject functions");
        }
        auto resolve = args[0].getObject(rt).getFunction(rt);
        resolve.call(rt, producer(rt));
        return Value::undefined();
      });
  return promiseCtor.callAsConstructor(runtime, executor);
}

maany_mpc_share_kind_t parseRole(Runtime& runtime, const Value& value) {
  if (!value.isString()) {
    throwTypeError(runtime, "role must be a string");
  }
  std::string role = value.getString(runtime).utf8(runtime);
  if (role == "device") return MAANY_MPC_SHARE_DEVICE;
  if (role == "server") return MAANY_MPC_SHARE_SERVER;
  throwTypeError(runtime, "role must be 'device' or 'server'");
  return MAANY_MPC_SHARE_DEVICE;
}

maany_mpc_sig_format_t parseSignatureFormat(Runtime& runtime, const Value& value) {
  if (value.isUndefined() || value.isNull()) {
    return MAANY_MPC_SIG_FORMAT_DER;
  }
  if (!value.isString()) {
    throwTypeError(runtime, "format must be 'der' or 'raw-rs'");
  }
  std::string fmt = value.getString(runtime).utf8(runtime);
  if (fmt == "der") return MAANY_MPC_SIG_FORMAT_DER;
  if (fmt == "raw-rs") return MAANY_MPC_SIG_FORMAT_RAW_RS;
  throwTypeError(runtime, "format must be 'der' or 'raw-rs'");
  return MAANY_MPC_SIG_FORMAT_DER;
}

class MaanyMpcHostObject final : public HostObject {
 public:
  Value get(Runtime& runtime, const PropNameID& nameId) override {
    auto name = nameId.utf8(runtime);

    if (name == "init") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "init"), 0,
          [](Runtime& rt, const Value&, const Value* /*args*/, size_t /*count*/) -> Value {
            maany_mpc_ctx_t* ctx = maany_mpc_init(nullptr);
            if (!ctx) {
              throw JSError(rt, "maany_mpc_init failed");
            }
            auto host = std::make_shared<CtxHostObject>(ctx);
            return Object::createFromHostObject(rt, host);
          });
    }

    if (name == "shutdown") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "shutdown"), 1,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 1) {
              throwTypeError(rt, "shutdown expects a context handle");
            }
            auto ctx = requireCtx(rt, args[0]);
            ctx->shutdown();
            return Value::undefined();
          });
    }

    if (name == "dkgNew") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "dkgNew"), 2,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 2) {
              throwTypeError(rt, "dkgNew expects (ctx, options)");
            }
            auto ctx = requireCtx(rt, args[0]);
            if (!args[1].isObject()) {
              throwTypeError(rt, "options must be an object");
            }
            auto optsObj = args[1].getObject(rt);

            maany_mpc_dkg_opts_t opts{};
            opts.curve = MAANY_MPC_CURVE_SECP256K1;
            opts.scheme = MAANY_MPC_SCHEME_ECDSA_2P;

            if (!optsObj.hasProperty(rt, "role")) {
              throwTypeError(rt, "options.role is required");
            }
            auto roleValue = optsObj.getProperty(rt, "role");
            opts.kind = parseRole(rt, roleValue);

            std::vector<uint8_t> sessionId;
            auto sessionValueOpt = getOptionalProperty(rt, optsObj, "sessionId");
            if (sessionValueOpt) {
              sessionId = toByteVector(rt, *sessionValueOpt, "sessionId");
              opts.session_id.data = sessionId.data();
              opts.session_id.len = sessionId.size();
            }

            auto keyIdValueOpt = getOptionalProperty(rt, optsObj, "keyId");
            if (keyIdValueOpt) {
              auto keyId = toByteVector(rt, *keyIdValueOpt, "keyId");
              if (keyId.size() != sizeof(opts.key_id_hint.bytes)) {
                throwTypeError(rt, "keyId must be 32 bytes");
              }
              std::memcpy(opts.key_id_hint.bytes, keyId.data(), keyId.size());
            }

            maany_mpc_dkg_t* dkg = nullptr;
            maany_mpc_error_t status = maany_mpc_dkg_new(ctx->ptr(rt), &opts, &dkg);
            if (status != MAANY_MPC_OK) {
              throwMaanyError(rt, "maany_mpc_dkg_new", status);
            }

            auto host = std::make_shared<DkgHostObject>(dkg);
            return Object::createFromHostObject(rt, host);
          });
    }

    if (name == "dkgStep") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "dkgStep"), 3,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 2) {
              throwTypeError(rt, "dkgStep expects (ctx, dkg, [inPeerMsg])");
            }
            auto ctx = requireCtx(rt, args[0]);
            auto dkg = requireDkg(rt, args[1]);

            std::vector<uint8_t> inbound;
            if (count >= 3 && !args[2].isUndefined() && !args[2].isNull()) {
              inbound = toByteVector(rt, args[2], "inPeerMsg");
            }

            maany_mpc_buf_t in_buf{nullptr, 0};
            if (!inbound.empty()) {
              in_buf.data = inbound.data();
              in_buf.len = inbound.size();
            }

            maany_mpc_buf_t out_buf{nullptr, 0};
            maany_mpc_step_result_t result = MAANY_MPC_STEP_CONTINUE;
            maany_mpc_error_t status = maany_mpc_dkg_step(ctx->ptr(rt), dkg->ptr(rt),
                                                          inbound.empty() ? nullptr : &in_buf,
                                                          &out_buf, &result);
            if (status != MAANY_MPC_OK) {
              if (out_buf.data) {
                maany_mpc_buf_free(ctx->ptr(rt), &out_buf);
              }
              throwMaanyError(rt, "maany_mpc_dkg_step", status);
            }

            std::vector<uint8_t> outbound;
            if (out_buf.data && out_buf.len) {
              auto* bytes = static_cast<uint8_t*>(out_buf.data);
              outbound.assign(bytes, bytes + out_buf.len);
            }
            if (out_buf.data) {
              maany_mpc_buf_free(ctx->ptr(rt), &out_buf);
            }

            bool done = (result == MAANY_MPC_STEP_DONE);
            return makeResolvedPromise(rt, [done, outbound = std::move(outbound)](Runtime& innerRt) mutable -> Value {
              auto stepResult = Object(innerRt);
              stepResult.setProperty(innerRt, "done", Value(done));
              if (!outbound.empty()) {
                stepResult.setProperty(innerRt, "outMsg", makeUint8Array(innerRt, outbound));
              }
              return stepResult;
            });
          });
    }

    if (name == "dkgFinalize") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "dkgFinalize"), 2,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 2) {
              throwTypeError(rt, "dkgFinalize expects (ctx, dkg)");
            }
            auto ctx = requireCtx(rt, args[0]);
            auto dkg = requireDkg(rt, args[1]);

            maany_mpc_keypair_t* kp = nullptr;
            maany_mpc_error_t status = maany_mpc_dkg_finalize(ctx->ptr(rt), dkg->ptr(rt), &kp);
            if (status != MAANY_MPC_OK) {
              throwMaanyError(rt, "maany_mpc_dkg_finalize", status);
            }
            dkg->free();

            auto host = std::make_shared<KeypairHostObject>(kp);
            return Object::createFromHostObject(rt, host);
          });
    }

    if (name == "dkgFree") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "dkgFree"), 1,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 1) {
              throwTypeError(rt, "dkgFree expects a DKG handle");
            }
            auto dkg = requireDkg(rt, args[0]);
            dkg->free();
            return Value::undefined();
          });
    }

    if (name == "backupCreate") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "backupCreate"), 3,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 2) {
              throwTypeError(rt, "backupCreate expects (ctx, keypair, [options])");
            }
            auto ctx = requireCtx(rt, args[0]);
            auto kp = requireKeypair(rt, args[1]);

            uint32_t threshold = 2;
            size_t shareCount = 3;
            std::vector<uint8_t> label;

            if (count >= 3 && !args[2].isUndefined() && !args[2].isNull()) {
              if (!args[2].isObject()) {
                throwTypeError(rt, "options must be an object");
              }
              auto opts = args[2].getObject(rt);
              if (auto thr = getOptionalProperty(rt, opts, "threshold")) {
                if (!thr->isNumber()) throwTypeError(rt, "threshold must be a number");
                threshold = static_cast<uint32_t>(thr->asNumber());
              }
              if (auto shareProp = getOptionalProperty(rt, opts, "shareCount")) {
                if (!shareProp->isNumber()) throwTypeError(rt, "shareCount must be a number");
                shareCount = static_cast<size_t>(shareProp->asNumber());
              } else if (auto shares = getOptionalProperty(rt, opts, "shares")) {
                if (!shares->isNumber()) throwTypeError(rt, "shares must be a number");
                shareCount = static_cast<size_t>(shares->asNumber());
              }
              if (auto labelProp = getOptionalProperty(rt, opts, "label")) {
                label = toByteVector(rt, *labelProp, "label");
              }
            }

            if (shareCount < threshold) {
              throwTypeError(rt, "shareCount must be >= threshold");
            }

            maany_mpc_buf_t labelBuf{nullptr, 0};
            if (!label.empty()) {
              labelBuf.data = label.data();
              labelBuf.len = label.size();
            }

            maany_mpc_backup_ciphertext_t cipher{};
            std::vector<maany_mpc_backup_share_t> shareStructs(shareCount);
            maany_mpc_error_t status = maany_mpc_backup_create(
              ctx->ptr(rt), kp->ptr(rt), threshold, shareCount,
              label.empty() ? nullptr : &labelBuf, &cipher, shareStructs.data());
            if (status != MAANY_MPC_OK) {
              throwMaanyError(rt, "maany_mpc_backup_create", status);
            }

            auto cipherObj = Object(rt);
            cipherObj.setProperty(rt, "kind", makeString(rt, shareKindToString(cipher.kind)));
            cipherObj.setProperty(rt, "curve", makeString(rt, curveToString(cipher.curve)));
            cipherObj.setProperty(rt, "scheme", makeString(rt, schemeToString(cipher.scheme)));
            cipherObj.setProperty(rt, "threshold", Value(static_cast<double>(cipher.threshold)));
            cipherObj.setProperty(rt, "shareCount", Value(static_cast<double>(cipher.share_count)));
            cipherObj.setProperty(
                rt,
                "keyId",
                makeUint8Array(
                    rt,
                    std::vector<uint8_t>(
                        cipher.key_id.bytes,
                        cipher.key_id.bytes + sizeof(cipher.key_id.bytes))));

            std::vector<uint8_t> labelOut;
            if (cipher.label.data && cipher.label.len) {
              auto* ptr = static_cast<uint8_t*>(cipher.label.data);
              labelOut.assign(ptr, ptr + cipher.label.len);
            }
            cipherObj.setProperty(rt, "label", makeUint8Array(rt, labelOut));

            std::vector<uint8_t> blobOut;
            if (cipher.ciphertext.data && cipher.ciphertext.len) {
              auto* ptr = static_cast<uint8_t*>(cipher.ciphertext.data);
              blobOut.assign(ptr, ptr + cipher.ciphertext.len);
            }
            cipherObj.setProperty(rt, "blob", makeUint8Array(rt, blobOut));

            maany_mpc_buf_free(ctx->ptr(rt), &cipher.label);
            maany_mpc_buf_free(ctx->ptr(rt), &cipher.ciphertext);

            auto sharesArray = Array(rt, shareCount);
            for (size_t i = 0; i < shareCount; ++i) {
              std::vector<uint8_t> shareBytes;
              if (shareStructs[i].data.data && shareStructs[i].data.len) {
                auto* ptr = static_cast<uint8_t*>(shareStructs[i].data.data);
                shareBytes.assign(ptr, ptr + shareStructs[i].data.len);
              }
              sharesArray.setValueAtIndex(rt, i, makeUint8Array(rt, shareBytes));
              maany_mpc_buf_free(ctx->ptr(rt), &shareStructs[i].data);
            }

            auto result = Object(rt);
            result.setProperty(rt, "ciphertext", cipherObj);
            result.setProperty(rt, "shares", sharesArray);
            return result;
          });
    }

    if (name == "backupRestore") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "backupRestore"), 3,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 3) {
              throwTypeError(rt, "backupRestore expects (ctx, ciphertext, shares)");
            }
            auto ctx = requireCtx(rt, args[0]);
            if (!args[1].isObject()) {
              throwTypeError(rt, "ciphertext must be an object");
            }
            auto cipherObj = args[1].getObject(rt);

            maany_mpc_backup_ciphertext_t cipher{};
            cipher.kind = parseShareKind(rt, cipherObj.getProperty(rt, "kind"));
            cipher.curve = parseCurve(rt, cipherObj.getProperty(rt, "curve"));
            cipher.scheme = parseScheme(rt, cipherObj.getProperty(rt, "scheme"));
            cipher.threshold = static_cast<uint32_t>(cipherObj.getProperty(rt, "threshold").asNumber());
            cipher.share_count = static_cast<uint32_t>(cipherObj.getProperty(rt, "shareCount").asNumber());

            auto keyIdVec = toByteVector(rt, cipherObj.getProperty(rt, "keyId"), "keyId");
            if (keyIdVec.size() != sizeof(cipher.key_id.bytes)) {
              throwTypeError(rt, "keyId must be 32 bytes");
            }
            std::memcpy(cipher.key_id.bytes, keyIdVec.data(), keyIdVec.size());

            auto labelVec = toByteVector(rt, cipherObj.getProperty(rt, "label"), "label");
            cipher.label.data = labelVec.data();
            cipher.label.len = labelVec.size();

            auto blobVec = toByteVector(rt, cipherObj.getProperty(rt, "blob"), "blob");
            if (blobVec.empty()) {
              throwTypeError(rt, "ciphertext blob must not be empty");
            }
            cipher.ciphertext.data = blobVec.data();
            cipher.ciphertext.len = blobVec.size();

            if (!args[2].isObject() || !args[2].getObject(rt).isArray(rt)) {
              throwTypeError(rt, "shares must be an array");
            }
            auto sharesArray = args[2].getObject(rt).getArray(rt);
            size_t shareCount = sharesArray.size(rt);
            if (shareCount < cipher.threshold) {
              throwTypeError(rt, "insufficient shares provided");
            }

            std::vector<std::vector<uint8_t>> shareStorage(shareCount);
            std::vector<maany_mpc_backup_share_t> shareStructs(shareCount);
            for (size_t i = 0; i < shareCount; ++i) {
              auto shareValue = sharesArray.getValueAtIndex(rt, i);
              shareStorage[i] = toByteVector(rt, shareValue, "share");
              if (shareStorage[i].empty()) {
                throwTypeError(rt, "share must not be empty");
              }
              shareStructs[i].data.data = shareStorage[i].data();
              shareStructs[i].data.len = shareStorage[i].size();
            }

            maany_mpc_keypair_t* restored = nullptr;
            maany_mpc_error_t status = maany_mpc_backup_restore(
              ctx->ptr(rt), &cipher, shareStructs.data(), shareStructs.size(), &restored);
            if (status != MAANY_MPC_OK) {
              throwMaanyError(rt, "maany_mpc_backup_restore", status);
            }

            auto host = std::make_shared<KeypairHostObject>(restored);
            return Object::createFromHostObject(rt, host);
          });
    }

    if (name == "kpExport") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "kpExport"), 2,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 2) {
              throwTypeError(rt, "kpExport expects (ctx, keypair)");
            }
            auto ctx = requireCtx(rt, args[0]);
            auto kp = requireKeypair(rt, args[1]);

            maany_mpc_buf_t blob{nullptr, 0};
            maany_mpc_error_t status = maany_mpc_kp_export(ctx->ptr(rt), kp->ptr(rt), &blob);
            if (status != MAANY_MPC_OK) {
              throwMaanyError(rt, "maany_mpc_kp_export", status);
            }

            std::vector<uint8_t> bytes;
            if (blob.data && blob.len) {
              auto* ptr = static_cast<uint8_t*>(blob.data);
              bytes.assign(ptr, ptr + blob.len);
            }
            maany_mpc_buf_free(ctx->ptr(rt), &blob);
            return makeUint8Array(rt, bytes);
          });
    }

    if (name == "kpImport") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "kpImport"), 2,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 2) {
              throwTypeError(rt, "kpImport expects (ctx, blob)");
            }
            auto ctx = requireCtx(rt, args[0]);
            auto blobVec = toByteVector(rt, args[1], "blob");

            maany_mpc_buf_t blob{blobVec.data(), blobVec.size()};
            maany_mpc_keypair_t* kp = nullptr;
            maany_mpc_error_t status = maany_mpc_kp_import(ctx->ptr(rt), &blob, &kp);
            if (status != MAANY_MPC_OK) {
              throwMaanyError(rt, "maany_mpc_kp_import", status);
            }

            auto host = std::make_shared<KeypairHostObject>(kp);
            return Object::createFromHostObject(rt, host);
          });
    }

    if (name == "kpPubkey") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "kpPubkey"), 2,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 2) {
              throwTypeError(rt, "kpPubkey expects (ctx, keypair)");
            }
            auto ctx = requireCtx(rt, args[0]);
            auto kp = requireKeypair(rt, args[1]);

            maany_mpc_pubkey_t pubkey{};
            maany_mpc_error_t status = maany_mpc_kp_pubkey(ctx->ptr(rt), kp->ptr(rt), &pubkey);
            if (status != MAANY_MPC_OK) {
              throwMaanyError(rt, "maany_mpc_kp_pubkey", status);
            }

            std::vector<uint8_t> bytes;
            if (pubkey.pubkey.data && pubkey.pubkey.len) {
              auto* ptr = static_cast<uint8_t*>(pubkey.pubkey.data);
              bytes.assign(ptr, ptr + pubkey.pubkey.len);
            }
            maany_mpc_buf_free(ctx->ptr(rt), &pubkey.pubkey);

            auto result = Object(rt);
            result.setProperty(rt, "curve", Value(static_cast<double>(pubkey.curve)));
            result.setProperty(rt, "compressed", makeUint8Array(rt, bytes));
            return result;
          });
    }

    if (name == "kpFree") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "kpFree"), 1,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 1) {
              throwTypeError(rt, "kpFree expects a keypair handle");
            }
            auto kp = requireKeypair(rt, args[0]);
            kp->free();
            return Value::undefined();
          });
    }

    if (name == "signNew") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "signNew"), 3,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 2) {
              throwTypeError(rt, "signNew expects (ctx, keypair, [opts])");
            }
            auto ctx = requireCtx(rt, args[0]);
            auto kp = requireKeypair(rt, args[1]);

            maany_mpc_sign_opts_t opts{};
            opts.scheme = MAANY_MPC_SCHEME_ECDSA_2P;

            std::vector<uint8_t> sessionId;
            std::vector<uint8_t> aad;

            if (count >= 3 && !args[2].isUndefined() && !args[2].isNull()) {
              if (!args[2].isObject()) {
                throwTypeError(rt, "sign options must be an object");
              }
              auto optObj = args[2].getObject(rt);

              auto sessionValue = getOptionalProperty(rt, optObj, "sessionId");
              if (sessionValue) {
                sessionId = toByteVector(rt, *sessionValue, "sessionId");
              }

              auto aadValue = getOptionalProperty(rt, optObj, "extraAad");
              if (aadValue) {
                aad = toByteVector(rt, *aadValue, "extraAad");
              }
            }

            if (!sessionId.empty()) {
              opts.session_id.data = sessionId.data();
              opts.session_id.len = sessionId.size();
            }
            if (!aad.empty()) {
              opts.extra_aad.data = aad.data();
              opts.extra_aad.len = aad.size();
            }

            maany_mpc_sign_t* sign = nullptr;
            maany_mpc_error_t status = maany_mpc_sign_new(ctx->ptr(rt), kp->ptr(rt), &opts, &sign);
            if (status != MAANY_MPC_OK) {
              throwMaanyError(rt, "maany_mpc_sign_new", status);
            }

            auto host = std::make_shared<SignHostObject>(sign);
            return Object::createFromHostObject(rt, host);
          });
    }

    if (name == "signSetMessage") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "signSetMessage"), 3,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 3) {
              throwTypeError(rt, "signSetMessage expects (ctx, sign, message)");
            }
            auto ctx = requireCtx(rt, args[0]);
            auto sign = requireSign(rt, args[1]);
            auto message = toByteVector(rt, args[2], "message");
            if (message.empty()) {
              throwTypeError(rt, "message must not be empty");
            }

            maany_mpc_error_t status = maany_mpc_sign_set_message(ctx->ptr(rt), sign->ptr(rt), message.data(), message.size());
            if (status != MAANY_MPC_OK) {
              throwMaanyError(rt, "maany_mpc_sign_set_message", status);
            }
            return Value::undefined();
          });
    }

    if (name == "signStep") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "signStep"), 3,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 2) {
              throwTypeError(rt, "signStep expects (ctx, sign, [inPeerMsg])");
            }
            auto ctx = requireCtx(rt, args[0]);
            auto sign = requireSign(rt, args[1]);

            std::vector<uint8_t> inbound;
            if (count >= 3 && !args[2].isUndefined() && !args[2].isNull()) {
              inbound = toByteVector(rt, args[2], "inPeerMsg");
            }

            maany_mpc_buf_t in_buf{nullptr, 0};
            if (!inbound.empty()) {
              in_buf.data = inbound.data();
              in_buf.len = inbound.size();
            }

            maany_mpc_buf_t out_buf{nullptr, 0};
            maany_mpc_step_result_t result = MAANY_MPC_STEP_CONTINUE;
            maany_mpc_error_t status = maany_mpc_sign_step(ctx->ptr(rt), sign->ptr(rt),
                                                           inbound.empty() ? nullptr : &in_buf,
                                                           &out_buf, &result);
            if (status != MAANY_MPC_OK) {
              if (out_buf.data) {
                maany_mpc_buf_free(ctx->ptr(rt), &out_buf);
              }
              throwMaanyError(rt, "maany_mpc_sign_step", status);
            }

            std::vector<uint8_t> outbound;
            if (out_buf.data && out_buf.len) {
              auto* ptr = static_cast<uint8_t*>(out_buf.data);
              outbound.assign(ptr, ptr + out_buf.len);
            }
            if (out_buf.data) {
              maany_mpc_buf_free(ctx->ptr(rt), &out_buf);
            }

            bool done = (result == MAANY_MPC_STEP_DONE);
            return makeResolvedPromise(rt, [done, outbound = std::move(outbound)](Runtime& innerRt) mutable -> Value {
              auto stepResult = Object(innerRt);
              stepResult.setProperty(innerRt, "done", Value(done));
              if (!outbound.empty()) {
                stepResult.setProperty(innerRt, "outMsg", makeUint8Array(innerRt, outbound));
              }
              return stepResult;
            });
          });
    }

    if (name == "signFinalize") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "signFinalize"), 3,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 2) {
              throwTypeError(rt, "signFinalize expects (ctx, sign, [format])");
            }
            auto ctx = requireCtx(rt, args[0]);
            auto sign = requireSign(rt, args[1]);
            auto format = (count >= 3) ? parseSignatureFormat(rt, args[2]) : MAANY_MPC_SIG_FORMAT_DER;

            maany_mpc_buf_t signature{nullptr, 0};
            maany_mpc_error_t status = maany_mpc_sign_finalize(ctx->ptr(rt), sign->ptr(rt), format, &signature);
            if (status != MAANY_MPC_OK) {
              throwMaanyError(rt, "maany_mpc_sign_finalize", status);
            }

            std::vector<uint8_t> bytes;
            if (signature.data && signature.len) {
              auto* ptr = static_cast<uint8_t*>(signature.data);
              bytes.assign(ptr, ptr + signature.len);
            }
            maany_mpc_buf_free(ctx->ptr(rt), &signature);
            return makeUint8Array(rt, bytes);
          });
    }

    if (name == "signFree") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "signFree"), 1,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 1) {
              throwTypeError(rt, "signFree expects a sign handle");
            }
            auto sign = requireSign(rt, args[0]);
            sign->free();
            return Value::undefined();
          });
    }

    if (name == "refreshNew") {
      return Function::createFromHostFunction(
          runtime, PropNameID::forAscii(runtime, "refreshNew"), 3,
          [](Runtime& rt, const Value&, const Value* args, size_t count) -> Value {
            if (count < 2) {
              throwTypeError(rt, "refreshNew expects (ctx, keypair, [opts])");
            }
            auto ctx = requireCtx(rt, args[0]);
            auto kp = requireKeypair(rt, args[1]);

            maany_mpc_refresh_opts_t opts{};
            std::vector<uint8_t> sessionId;
            if (count >= 3 && !args[2].isUndefined() && !args[2].isNull()) {
              if (!args[2].isObject()) {
                throwTypeError(rt, "refresh options must be an object");
              }
              auto optObj = args[2].getObject(rt);
              auto sessionValue = getOptionalProperty(rt, optObj, "sessionId");
              if (sessionValue) {
                sessionId = toByteVector(rt, *sessionValue, "sessionId");
                opts.session_id.data = sessionId.data();
                opts.session_id.len = sessionId.size();
              }
            }

            maany_mpc_dkg_t* refresher = nullptr;
            maany_mpc_error_t status = maany_mpc_refresh_new(ctx->ptr(rt), kp->ptr(rt), &opts, &refresher);
            if (status != MAANY_MPC_OK) {
              throwMaanyError(rt, "maany_mpc_refresh_new", status);
            }

            auto host = std::make_shared<DkgHostObject>(refresher);
            return Object::createFromHostObject(rt, host);
          });
    }

    return Value::undefined();
  }

  std::vector<PropNameID> getPropertyNames(Runtime& runtime) override {
    static const char* kProps[] = {
        "init",        "shutdown",    "dkgNew",        "dkgStep",      "dkgFinalize", "dkgFree",
        "kpExport",    "kpImport",    "kpPubkey",      "kpFree",       "signNew",     "signSetMessage",
        "signStep",    "signFinalize", "signFree",      "refreshNew",   "backupCreate", "backupRestore"};
    std::vector<PropNameID> names;
    names.reserve(sizeof(kProps) / sizeof(kProps[0]));
    for (const char* prop : kProps) {
      names.emplace_back(PropNameID::forAscii(runtime, prop));
    }
    return names;
  }
};

}  // namespace

void installMaanyMpc(Runtime& runtime) {
  auto host = std::make_shared<MaanyMpcHostObject>();
  runtime.global().setProperty(runtime, kBindingGlobalName, Object::createFromHostObject(runtime, host));
}

}  // namespace maany::rn

#endif  // MAANY_MPC_HOST_OBJECT_IMPL
