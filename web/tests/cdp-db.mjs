import fs from 'node:fs';

const base = process.env.CDP_URL || 'http://127.0.0.1:9223';
const appUrl = process.env.APP_URL || 'http://127.0.0.1:8765/';
const databaseBase64 = fs.readFileSync(new URL('./fixtures/test.db', import.meta.url)).toString('base64');

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
  if (message.method === 'Log.entryAdded' && message.params.entry.level === 'error') {
    errors.push(message.params.entry.text);
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
await send('Log.enable');
await send('Page.enable');
await send('Page.navigate', { url: appUrl });
await wait(2000);
await evaluate('localStorage.clear(); location.reload(); true');
await wait(2000);

const result = await evaluate(`(() => {
  const binary = atob(${JSON.stringify(databaseBase64)});
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i += 1) bytes[i] = binary.charCodeAt(i);
  const file = new File([bytes], 'test.db', { type: 'application/x-sqlite3' });
  const transfer = new DataTransfer();
  transfer.items.add(file);
  const input = document.querySelector('#file-input');
  input.files = transfer.files;
  input.dispatchEvent(new Event('change', { bubbles: true }));
  return { size: file.size, name: file.name };
})()`);
await wait(3500);

const summary = await evaluate(`({
  title: document.querySelector('#detail-title')?.textContent,
  count: document.querySelector('#entry-count')?.textContent,
  name: document.querySelector('#name')?.value,
  notes: document.querySelector('#notes')?.value,
  meta: document.querySelector('#meta')?.textContent,
  favorite: document.querySelector('#favorite')?.textContent,
  portraitVisible: !document.querySelector('#portrait-image')?.hidden,
  portraitPrefix: document.querySelector('#portrait-image')?.src?.slice(0, 30),
  base64Length: document.querySelector('#base64-code')?.value?.length,
  modal: document.querySelector('.modal-backdrop .dialog-title')?.textContent,
  modalText: document.querySelector('.modal-backdrop .dialog-body')?.textContent?.slice(0, 300),
})`);

console.log(JSON.stringify({ upload: result, summary }, null, 2));
console.log('ERRORS');
for (const error of errors) console.log(error);
ws.close();
process.exit(errors.length ? 1 : 0);
