export declare function cloneBytes(source: Uint8Array): Uint8Array;
export declare function optionalClone(source?: Uint8Array | null): Uint8Array | undefined;
export declare function concatBytes(parts: readonly Uint8Array[]): Uint8Array;
export declare function fromUtf8(text: string): Uint8Array;
export declare function toHex(bytes: Uint8Array): string;
export declare function toBase64(bytes: Uint8Array): string;
export declare function fromBase64(value: string): Uint8Array;
