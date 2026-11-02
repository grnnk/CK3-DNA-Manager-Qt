import fs from 'node:fs';
import path from 'node:path';

const base = process.env.CDP_URL || 'http://127.0.0.1:9223';
const appUrl = process.env.APP_URL || 'http://127.0.0.1:8765/';
const downloadDir = process.env.DOWNLOAD_DIR || 'C:\\Users\\Naoki\\AppData\\Local\\Temp\\opencode\\ck3dna-downloads';
fs.mkdirSync(downloadDir, { recursive: true });
for (const file of fs.readdirSync(downloadDir)) fs.rmSync(path.join(downloadDir, file), { force: true });

const list = await fetch(`${base}/json/list`).then((response) => response.json());
const page = list.find((item) => item.type === 'page');
if (!page) throw new Error('No page target');
const ws = new WebSocket(page.webSocketDebuggerUrl);
let nextId = 1;
const pending = new Map();
const errors = [];
ws.addEventListener('message', (event) => {
  const message = JSON.parse(event.data);
  if (message.id) {
    const resolver = pending.get(message.id);
    if (resolver) {
      pending.delete(message.id);
      if (message.error) resolver.reject(new Error(message.error.message));
      else resolver.resolve(message.result);
    }
    return;
  }
  if (message.method === 'Runtime.exceptionThrown') {
    errors.push(message.params.exceptionDetails?.exception?.description || message.params.exceptionDetails?.text);
  }
  if (message.method === 'Runtime.consoleAPICalled' && message.params.type === 'error') {
    errors.push(message.params.args.map((arg) => arg.value ?? arg.description ?? '').join(' '));
  }
});
await new Promise((resolve, reject) => {
  ws.addEventListener('open', resolve, { once: true });
  ws.addEventListener('error', reject, { once: true });
});
function send(method, params = {}) {
  const id = nextId++;
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    ws.send(JSON.stringify({ id, method, params }));
  });
}
async function evaluate(expression) {
  const result = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
  if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
  return result.result.value;
}
const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
await send('Runtime.enable');
await send('Page.enable');
await send('Browser.setDownloadBehavior', { behavior: 'allow', downloadPath: downloadDir, eventsEnabled: true });
await send('Page.navigate', { url: appUrl });
await wait(2000);

// Ensure there is an entry by importing the JSON fixture if the library is empty.
const count = await evaluate(`document.querySelectorAll('.entry-item').length`);
if (count === 0) {
  await evaluate(`(async () => {
    const response = await fetch('./tests/fixtures/export.json');
    const blob = await response.blob();
    const file = new File([blob], 'export.json', { type: 'application/json' });
    const transfer = new DataTransfer();
    transfer.items.add(file);
    const input = document.querySelector('#file-input');
    input.files = transfer.files;
    input.dispatchEvent(new Event('change', { bubbles: true }));
    return true;
  })()`);
  await wait(2500);
  await evaluate(`document.querySelector('.modal-backdrop [data-close]')?.click(); true`);
  await wait(300);
}

await evaluate(`document.querySelector('#export-entry').click(); true`);
await wait(2000);

const files = fs.readdirSync(downloadDir);
let summary = null;
if (files.length) {
  const file = path.join(downloadDir, files[0]);
  const root = JSON.parse(fs.readFileSync(file, 'utf8'));
  const entry = root.entries?.[0];
  summary = {
    file: files[0],
    format: root.format,
    version: root.version,
    entries: root.entries?.length,
    name: entry?.name,
    hasBase64: typeof entry?.base64 === 'string' && entry.base64.length > 0,
    base64Length: entry?.base64?.length,
    portraitPngLength: entry?.portraitPng?.length,
    portraitPngPrefix: entry?.portraitPng?.slice(0, 12),
    rulerDesigner: entry?.rulerDesigner?.slice(0, 40),
    geneCount: entry?.geneCount,
  };
}
console.log(JSON.stringify({ files, summary }, null, 2));
console.log('ERRORS');
for (const error of errors) console.log(error);
ws.close();
process.exit(errors.length || !summary ? 1 : 0);
