#include <jni.h>
#include <jsi/jsi.h>

#include "MaanyMpcHostObject.h"

using facebook::jsi::Runtime;

extern "C"
JNIEXPORT void JNICALL
Java_com_maany_mpc_MaanyMpcModule_nativeInstall(JNIEnv* /*env*/, jclass /*type*/, jlong runtimePtr) {
  if (runtimePtr == 0) {
    return;
  }
  auto* runtime = reinterpret_cast<Runtime*>(runtimePtr);
  maany::rn::installMaanyMpc(*runtime);
}
