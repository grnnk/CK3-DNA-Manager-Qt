import fs from 'node:fs';
import path from 'node:path';

const base = process.env.CDP_URL || 'http://127.0.0.1:9223';
const appUrl = process.env.APP_URL || 'http://127.0.0.1:8765/';
const outFile = process.env.SCREENSHOT || 'C:\\Users\\Naoki\\AppData\\Local\\Temp\\opencode\\ck3dna-web.png';
const fixture = fs.readFileSync(new URL('./fixtures/export.json', import.meta.url), 'utf8');

const list = await fetch(`${base}/json/list`).then((response) => response.json());
const page = list.filter((item) => item.type === 'page').at(-1);
if (!page) throw new Error('No page target');
const ws = new WebSocket(page.webSocketDebuggerUrl);
let nextId = 1;
const pending = new Map();
ws.addEventListener('message', (event) => {
  const message = JSON.parse(event.data);
  if (message.id) {
    const resolver = pending.get(message.id);
    if (resolver) {
      pending.delete(message.id);
      if (message.error) resolver.reject(new Error(message.error.message));
      else resolver.resolve(message.result);
    }
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
try {
  await send('Emulation.setDeviceMetricsOverride', {
    width: 1366,
    height: 768,
    deviceScaleFactor: 1,
    mobile: false,
  });
} catch {
  // The target may already use the browser window size.
}
await send('Network.enable');
await send('Network.setCacheDisabled', { cacheDisabled: true });
await send('Page.navigate', { url: appUrl });
await wait(2000);
await evaluate('localStorage.clear(); location.reload(); true');
await wait(2000);
await evaluate(`(() => {
  const file = new File([${JSON.stringify(fixture)}], 'export.json', { type: 'application/json' });
  const transfer = new DataTransfer();
  transfer.items.add(file);
  const input = document.querySelector('#file-input');
  input.files = transfer.files;
  input.dispatchEvent(new Event('change', { bubbles: true }));
  return true;
})()`);
await wait(2500);
await evaluate(`document.querySelector('.modal-backdrop [data-close]')?.click(); true`);
await wait(500);
if (process.env.SCREENSHOT_VIEW === 'genes') {
  await evaluate(`document.querySelector('#tab-genes').click(); true`);
  await wait(500);
}
const screenshot = await send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false });
fs.mkdirSync(path.dirname(outFile), { recursive: true });
fs.writeFileSync(outFile, Buffer.from(screenshot.data, 'base64'));
console.log(outFile);
ws.close();
