#import "MaanyMpcSecureStorage.h"

#import <React/RCTUtils.h>
#import <Security/Security.h>

static NSString *const kMaanyService = @"com.maany.mpc.share";

@implementation MaanyMpcSecureStorage

RCT_EXPORT_MODULE();

- (dispatch_queue_t)methodQueue
{
  return dispatch_get_main_queue();
}

RCT_EXPORT_METHOD(saveShare:(NSString *)keyId
                  data:(NSString *)base64
                  resolver:(RCTPromiseResolveBlock)resolve
                  rejecter:(RCTPromiseRejectBlock)reject)
{
  NSData *value = [[NSData alloc] initWithBase64EncodedString:base64 options:0];
  if (!value) {
    reject(@"ERR_INVALID_BASE64", @"Failed to decode base64", nil);
    return;
  }

  NSDictionary *query = @{
    (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
    (__bridge id)kSecAttrService : kMaanyService,
    (__bridge id)kSecAttrAccount : keyId,
  };
  SecItemDelete((__bridge CFDictionaryRef)query);

  CFErrorRef error = NULL;
  SecAccessControlRef access = SecAccessControlCreateWithFlags(
      NULL,
      kSecAttrAccessibleWhenUnlocked,
      kSecAccessControlUserPresence | kSecAccessControlBiometryCurrentSet,
      &error);
  if (access == NULL) {
    if (error) {
      CFRelease(error);
      error = NULL;
    }
    access = SecAccessControlCreateWithFlags(
        NULL,
        kSecAttrAccessibleWhenUnlocked,
        kSecAccessControlUserPresence,
        &error);
  }
  if (access == NULL) {
    NSError *err = CFBridgingRelease(error);
    reject(@"ERR_ACCESS_CONTROL", @"Failed to create access control", err);
    return;
  }

  NSDictionary *attributes = @{
    (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
    (__bridge id)kSecAttrService : kMaanyService,
    (__bridge id)kSecAttrAccount : keyId,
    (__bridge id)kSecValueData : value,
    (__bridge id)kSecAttrAccessControl : (__bridge id)access,
  };

  OSStatus status = SecItemAdd((__bridge CFDictionaryRef)attributes, NULL);
  CFRelease(access);

  if (status != errSecSuccess) {
    reject(@"ERR_SAVE_SHARE", [self messageForStatus:status], nil);
    return;
  }
  resolve(nil);
}

RCT_EXPORT_METHOD(loadShare:(NSString *)keyId
                  prompt:(NSString *)prompt
                  resolver:(RCTPromiseResolveBlock)resolve
                  rejecter:(RCTPromiseRejectBlock)reject)
{
  NSString *operationPrompt = prompt ?: @"Authenticate to access your key share";
  NSDictionary *query = @{
    (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
    (__bridge id)kSecAttrService : kMaanyService,
    (__bridge id)kSecAttrAccount : keyId,
    (__bridge id)kSecReturnData : @YES,
    (__bridge id)kSecUseOperationPrompt : operationPrompt,
  };

  CFTypeRef result = NULL;
  OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
  if (status == errSecItemNotFound) {
    resolve([NSNull null]);
    return;
  }
  if (status != errSecSuccess) {
    if (result) {
      CFRelease(result);
    }
    reject(@"ERR_LOAD_SHARE", [self messageForStatus:status], nil);
    return;
  }

  NSData *data = CFBridgingRelease(result);
  NSString *base64 = [data base64EncodedStringWithOptions:0];
  resolve(base64);
}

RCT_EXPORT_METHOD(removeShare:(NSString *)keyId
                  resolver:(RCTPromiseResolveBlock)resolve
                  rejecter:(RCTPromiseRejectBlock)reject)
{
  NSDictionary *query = @{
    (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
    (__bridge id)kSecAttrService : kMaanyService,
    (__bridge id)kSecAttrAccount : keyId,
  };

  OSStatus status = SecItemDelete((__bridge CFDictionaryRef)query);
  if (status != errSecSuccess && status != errSecItemNotFound) {
    reject(@"ERR_REMOVE_SHARE", [self messageForStatus:status], nil);
    return;
  }
  resolve(nil);
}

- (NSString *)messageForStatus:(OSStatus)status
{
  NSString *message = (__bridge_transfer NSString *)SecCopyErrorMessageString(status, NULL);
  if (message) {
    return message;
  }
  switch (status) {
    case errSecAuthFailed:
      return @"Authentication failed";
    case errSecUserCanceled:
      return @"Authentication canceled";
    default:
      return [NSString stringWithFormat:@"Keychain error (%d)", (int)status];
  }
}

@end
