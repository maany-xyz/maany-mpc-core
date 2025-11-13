package com.maany.mpc;

import androidx.annotation.NonNull;

import com.facebook.react.bridge.JavaScriptContextHolder;
import com.facebook.react.bridge.ReactApplicationContext;
import com.facebook.react.bridge.ReactContextBaseJavaModule;
import com.facebook.react.bridge.ReactMethod;

public class MaanyMpcModule extends ReactContextBaseJavaModule {
  static {
    System.loadLibrary("maany_mpc_rn");
  }

  private boolean installed = false;

  public MaanyMpcModule(ReactApplicationContext reactContext) {
    super(reactContext);
  }

  @NonNull
  @Override
  public String getName() {
    return "MaanyMpc";
  }

  @ReactMethod(isBlockingSynchronousMethod = true)
  public boolean install() {
    if (installed) {
      return true;
    }

    final ReactApplicationContext context = getReactApplicationContext();
    JavaScriptContextHolder holder = context.getJavaScriptContextHolder();
    long runtimePtr = holder == null ? 0 : holder.get();
    if (runtimePtr == 0) {
      throw new RuntimeException("MaanyMpc: JSI runtime not available");
    }
    nativeInstall(runtimePtr);
    installed = true;
    return true;
  }

  private static native void nativeInstall(long runtimePtr);
}
