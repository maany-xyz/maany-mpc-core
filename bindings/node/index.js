const fs = require('node:fs');
const path = require('node:path');

function resolveBinding() {
  const localPath = path.join(__dirname, 'maany_mpc_node.node');
  if (fs.existsSync(localPath)) return localPath;
  const buildPath = path.join(__dirname, '..', '..', 'build', 'bindings', 'node', 'maany_mpc_node.node');
  if (fs.existsSync(buildPath)) return buildPath;
  throw new Error(`Unable to locate maany_mpc_node.node (looked in ${localPath} and ${buildPath})`);
}

const binding = require(resolveBinding());

module.exports = {
  init: binding.init,
  shutdown: binding.shutdown,
  dkgNew: binding.dkgNew,
  dkgStep: binding.dkgStep,
  dkgFinalize: binding.dkgFinalize,
  dkgFree: binding.dkgFree,
  kpExport: binding.kpExport,
  kpImport: binding.kpImport,
  kpPubkey: binding.kpPubkey,
  kpFree: binding.kpFree,
  signNew: binding.signNew,
  signSetMessage: binding.signSetMessage,
  signStep: binding.signStep,
  signFinalize: binding.signFinalize,
  signFree: binding.signFree,
  refreshNew: binding.refreshNew,
  backupCreate: binding.backupCreate,
  backupRestore: binding.backupRestore
};
