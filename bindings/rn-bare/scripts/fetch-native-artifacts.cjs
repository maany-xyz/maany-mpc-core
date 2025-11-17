#!/usr/bin/env node

const fs = require('fs');
const path = require('path');
const os = require('os');
const http = require('http');
const https = require('https');
const { spawn } = require('child_process');
const { fileURLToPath } = require('url');

const rootDir = path.resolve(__dirname, '..');
const manifestPath = path.join(rootDir, 'native-artifacts.json');
const packageJson = require(path.join(rootDir, 'package.json'));

if (!fs.existsSync(manifestPath)) {
  console.error(`maany-mpc: Missing manifest at ${manifestPath}`);
  process.exit(1);
}

const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
const baseUrl = process.env.MAANY_MPC_ARTIFACTS_BASE_URL
  || `https://github.com/maany/maany-mpc-core/releases/download/v${packageJson.version}`;
const forceDownload = process.env.MAANY_MPC_FORCE_DOWNLOAD === '1';

function expandTemplate(value) {
  return value.replace(/\$\{version\}/g, packageJson.version);
}

function isHttpUrl(value) {
  try {
    const parsed = new URL(value);
    return parsed.protocol === 'http:' || parsed.protocol === 'https:';
  } catch (_err) {
    return false;
  }
}

function isFileUrl(value) {
  try {
    const parsed = new URL(value);
    return parsed.protocol === 'file:';
  } catch (_err) {
    return false;
  }
}

function resolveLocalPath(value) {
  if (path.isAbsolute(value)) {
    return value;
  }
  return path.resolve(process.cwd(), value);
}

function download(url, destination) {
  const client = url.startsWith('https:') ? https : http;
  return new Promise((resolve, reject) => {
    const request = client.get(url, response => {
      if (response.statusCode && response.statusCode >= 300 && response.statusCode < 400 && response.headers.location) {
        response.destroy();
        download(response.headers.location, destination).then(resolve).catch(reject);
        return;
      }
      if (response.statusCode !== 200) {
        reject(new Error(`Request for ${url} failed with status ${response.statusCode}`));
        return;
      }
      const stream = fs.createWriteStream(destination);
      response.pipe(stream);
      stream.on('finish', () => {
        stream.close(resolve);
      });
      stream.on('error', reject);
    });
    request.on('error', reject);
  });
}

function extractTarGz(archivePath, destination) {
  return new Promise((resolve, reject) => {
    const child = spawn('tar', ['-xzf', archivePath, '-C', destination], { stdio: 'inherit' });
    child.on('error', reject);
    child.on('close', code => {
      if (code === 0) {
        resolve();
      } else {
        reject(new Error(`tar exited with code ${code}`));
      }
    });
  });
}

async function installArtifact(artifact) {
  const checkPath = path.join(rootDir, artifact.check);
  if (!forceDownload && fs.existsSync(checkPath)) {
    return;
  }

  const template = expandTemplate(artifact.archive);
  const override = artifact.envVar ? process.env[artifact.envVar] : undefined;
  const source = override || `${baseUrl}/${template}`;

  for (const relative of artifact.outputs || []) {
    const absPath = path.join(rootDir, relative);
    await fs.promises.rm(absPath, { recursive: true, force: true });
  }

  const tmpDir = await fs.promises.mkdtemp(path.join(os.tmpdir(), 'maany-mpc-'));
  const tmpArchive = path.join(tmpDir, path.basename(source) || `${artifact.name}.tar.gz`);

  if (isHttpUrl(source)) {
    console.log(`maany-mpc: downloading ${artifact.name} from ${source}`);
    await download(source, tmpArchive);
  } else if (isFileUrl(source)) {
    const filePath = fileURLToPath(source);
    await fs.promises.copyFile(filePath, tmpArchive);
  } else {
    const absolute = resolveLocalPath(source);
    await fs.promises.copyFile(absolute, tmpArchive);
  }

  await extractTarGz(tmpArchive, rootDir);
  await fs.promises.rm(tmpDir, { recursive: true, force: true });

  if (!fs.existsSync(checkPath)) {
    throw new Error(`Downloaded archive for ${artifact.name} did not create ${artifact.check}`);
  }
}

(async () => {
  for (const artifact of manifest) {
    try {
      await installArtifact(artifact);
    } catch (err) {
      console.error(`maany-mpc: Failed to install ${artifact.name}: ${err.message}`);
      process.exitCode = 1;
      return;
    }
  }
})();
