export interface SignDoc {
    chainId: string;
    accountNumber: string;
    sequence: string;
    bodyBytes: Uint8Array;
    authInfoBytes: Uint8Array;
}
export declare function makeSignBytes(doc: SignDoc): Uint8Array;
export declare function sha256(data: Uint8Array): Uint8Array;
