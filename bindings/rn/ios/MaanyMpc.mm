#import <React/RCTBridge+Private.h>
#import <React/RCTBridgeModule.h>
#import <React/RCTLog.h>
#import <React/RCTUtils.h>

#import "../cpp/MaanyMpcHostObject.h"
using namespace facebook;

@interface MaanyMpc : NSObject <RCTBridgeModule>
@end

@implementation MaanyMpc
{
  bool _installed;
}

RCT_EXPORT_MODULE(MaanyMpc)

@synthesize bridge = _bridge;

- (dispatch_queue_t)methodQueue
{
  return RCTJSThread;
}

RCT_EXPORT_METHOD(install)
{
  if (_installed) {
    return;
  }

  RCTCxxBridge *cxxBridge = (RCTCxxBridge *)self.bridge;
  if (cxxBridge == nil) {
    RCTLogError(@"MaanyMpc: bridge is not initialized");
    return;
  }

  jsi::Runtime *runtime = reinterpret_cast<jsi::Runtime *>(cxxBridge.runtime);
  if (runtime == nullptr) {
    RCTLogError(@"MaanyMpc: JSI runtime not available");
    return;
  }

  maany::rn::installMaanyMpc(*runtime);
  _installed = true;
}

@end
