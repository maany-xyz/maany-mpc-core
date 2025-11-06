declare module 'react-native' {
  interface NativeModuleWithInstall {
    install?: () => void;
    saveShare?(keyId: string, base64: string): Promise<void>;
    loadShare?(keyId: string, prompt?: string): Promise<string | null>;
    removeShare?(keyId: string): Promise<void>;
    [key: string]: unknown;
  }

  interface NativeModulesShape {
    MaanyMpc?: NativeModuleWithInstall;
    MaanyMpcSecureStorage?: NativeModuleWithInstall;
    [key: string]: NativeModuleWithInstall | undefined;
  }

  export const NativeModules: NativeModulesShape;
  export const Platform: {
    OS: string;
  };
}
