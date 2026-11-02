const base = process.env.CDP_URL || 'http://127.0.0.1:9223';
const appUrl = process.env.APP_URL || 'http://127.0.0.1:8765/';
const dna = Buffer.alloc(416).toString('base64');

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

// Clean state and OPFS.
await evaluate(`(async () => {
  localStorage.clear();
  const root = await navigator.storage.getDirectory();
  try { await root.removeEntry('web-sync', { recursive: true }); } catch {}
  await new Promise((resolve) => {
    const request = indexedDB.deleteDatabase('ck3dna.v1.handles');
    request.onsuccess = request.onerror = request.onblocked = () => resolve();
  });
  return true;
})()`);
await wait(300);

// Remember an OPFS directory handle and set folder startup source.
await evaluate(`(async () => {
  const root = await navigator.storage.getDirectory();
  const Folder = await import('./js/storage/folder-store.js');
  await Folder.saveDirectoryHandle(root);
  localStorage.setItem('ck3dna.v1.settings', JSON.stringify({ language: 'en', sort: 'updated', startupSource: 'folder' }));
  return true;
})()`);
await evaluate('location.reload(); true');
await wait(3000);

const afterBoot = await evaluate(`(async () => {
  const root = await navigator.storage.getDirectory();
  const Folder = await import('./js/storage/folder-store.js');
  const sync = await Folder.readSyncLibrary(root);
  return {
    count: document.querySelector('#entry-count')?.textContent,
    syncExists: Boolean(sync),
    syncEntries: sync?.entries?.length,
    status: document.querySelector('#status-text')?.textContent,
  };
})()`);

// Create an entry through the UI and wait for the debounced folder write.
await evaluate(`document.querySelector('#new-button').click(); true`);
await wait(300);
await evaluate(`(() => {
  const name = document.querySelector('#add-name');
  name.value = 'Folder Sync Test';
  name.dispatchEvent(new Event('input', { bubbles: true }));
  const area = document.querySelector('#add-dna');
  area.value = ${JSON.stringify(dna)};
  area.dispatchEvent(new Event('input', { bubbles: true }));
  return true;
})()`);
await wait(700);
await evaluate(`document.querySelector('#add-create').click(); true`);
await wait(2500);

const afterCreate = await evaluate(`(async () => {
  const root = await navigator.storage.getDirectory();
  const Folder = await import('./js/storage/folder-store.js');
  const sync = await Folder.readSyncLibrary(root);
  return {
    count: document.querySelector('#entry-count')?.textContent,
    syncEntries: sync?.entries?.length,
    syncName: sync?.entries?.[0]?.name,
    syncHasBase64: Boolean(sync?.entries?.[0]?.base64),
    syncId: sync?.entries?.[0]?.id,
  };
})()`);

// Write a folder entry directly and check that a reload imports it.
await evaluate(`(async () => {
  const root = await navigator.storage.getDirectory();
  const Folder = await import('./js/storage/folder-store.js');
  const current = await Folder.readSyncLibrary(root);
  current.entries.push({
    id: 'folder-only-entry',
    name: 'Folder Only Entry',
    notes: 'Pulled from web-sync',
    favorite: false,
    base64: ${JSON.stringify(dna)},
    geneCount: 104,
    moddedGeneCount: 0,
    createdAt: 1700000000000,
    updatedAt: 1700000001000,
  });
  await Folder.writeSyncLibrary(root, current);
  localStorage.removeItem('ck3dna.v1.library');
  return true;
})()`);
await evaluate('location.reload(); true');
await wait(3500);

const afterPull = await evaluate(`({
  count: document.querySelector('#entry-count')?.textContent,
  names: [...document.querySelectorAll('.entry-name')].map((item) => item.textContent),
  title: document.querySelector('#detail-title')?.textContent,
})`);

// Clean up the test folder and remembered handle.
await evaluate(`(async () => {
  const root = await navigator.storage.getDirectory();
  try { await root.removeEntry('web-sync', { recursive: true }); } catch {}
  const Folder = await import('./js/storage/folder-store.js');
  try { await Folder.clearDirectoryHandle(); } catch {}
  localStorage.clear();
  return true;
})()`);

console.log(JSON.stringify({ afterBoot, afterCreate, afterPull }, null, 2));
console.log('ERRORS');
for (const error of errors) console.log(error);
ws.close();
process.exit(errors.length ? 1 : 0);
