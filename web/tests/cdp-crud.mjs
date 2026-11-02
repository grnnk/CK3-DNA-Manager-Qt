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
const logs = [];

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
  if (message.method === 'Runtime.consoleAPICalled') {
    const text = message.params.args.map((arg) => arg.value ?? arg.description ?? '').join(' ');
    logs.push(`${message.params.type}: ${text}`);
    if (message.params.type === 'error') errors.push(text);
  }
  if (message.method === 'Log.entryAdded') {
    const entry = message.params.entry;
    logs.push(`${entry.level}: ${entry.text}`);
    if (entry.level === 'error') errors.push(entry.text);
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

const steps = {};

steps.initial = await evaluate(`({
  title: document.title,
  count: document.querySelector('#entry-count')?.textContent,
  welcome: !document.querySelector('#welcome')?.hidden,
})`);

await evaluate(`document.querySelector('#new-button').click(); true`);
await wait(300);
steps.dialogOpen = await evaluate(`Boolean(document.querySelector('#add-dna'))`);

await evaluate(`(() => {
  const name = document.querySelector('#add-name');
  name.value = 'Smoke Test';
  name.dispatchEvent(new Event('input', { bubbles: true }));
  const area = document.querySelector('#add-dna');
  area.value = ${JSON.stringify(dna)};
  area.dispatchEvent(new Event('input', { bubbles: true }));
  return true;
})()`);
await wait(700);
steps.validation = await evaluate(`({
  status: document.querySelector('#add-status')?.textContent,
  disabled: document.querySelector('#add-create')?.disabled,
})`);

await evaluate(`document.querySelector('#add-create').click(); true`);
await wait(900);
steps.created = await evaluate(`({
  title: document.querySelector('#detail-title')?.textContent,
  count: document.querySelector('#entry-count')?.textContent,
  contentHidden: document.querySelector('#content')?.hidden,
  name: document.querySelector('#name')?.value,
  meta: document.querySelector('#meta')?.textContent,
  listItems: document.querySelectorAll('.entry-item').length,
})`);

await evaluate(`document.querySelector('#tab-genes').click(); true`);
await wait(300);
steps.genes = await evaluate(`({
  rows: document.querySelectorAll('.gene-row').length,
  sections: document.querySelectorAll('.gene-section').length,
  count: document.querySelector('#gene-count')?.textContent,
  dirty: document.querySelector('#dirty-label')?.textContent,
})`);

await evaluate(`(() => {
  const input = document.querySelector('.gene-row input[type="number"]');
  if (input) {
    input.value = '7';
    input.dispatchEvent(new Event('input', { bubbles: true }));
  }
  return true;
})()`);
await wait(150);
steps.dirtyAfterEdit = await evaluate(`({
  dirty: document.querySelector('#dirty-label')?.textContent,
  saveDisabled: document.querySelector('#save-genes')?.disabled,
})`);

await evaluate(`document.querySelector('#save-genes').click(); true`);
await wait(700);
steps.savedGenes = await evaluate(`({
  dirty: document.querySelector('#dirty-label')?.textContent,
  saveDisabled: document.querySelector('#save-genes')?.disabled,
  count: document.querySelector('#entry-count')?.textContent,
})`);

await evaluate(`(() => {
  document.querySelector('#tab-overview').click();
  const name = document.querySelector('#name');
  name.value = 'Smoke Test 2';
  name.dispatchEvent(new Event('input', { bubbles: true }));
  document.querySelector('#save-details').click();
  return true;
})()`);
await wait(700);
steps.renamed = await evaluate(`({
  title: document.querySelector('#detail-title')?.textContent,
  name: document.querySelector('#name')?.value,
})`);

await evaluate('location.reload(); true');
await wait(2500);
steps.reloaded = await evaluate(`({
  title: document.querySelector('#detail-title')?.textContent,
  count: document.querySelector('#entry-count')?.textContent,
  name: document.querySelector('#name')?.value,
  rows: document.querySelectorAll('.entry-item').length,
})`);

await evaluate(`document.querySelector('#favorite').click(); true`);
await wait(400);
steps.favorite = await evaluate(`({
  star: document.querySelector('.entry-star')?.textContent,
  favorite: document.querySelector('#favorite')?.textContent,
})`);

console.log(JSON.stringify(steps, null, 2));
console.log('ERRORS');
for (const error of errors) console.log(error);
ws.close();
process.exit(errors.length ? 1 : 0);
