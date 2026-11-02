const base = process.env.CDP_URL || 'http://127.0.0.1:9223';
const appUrl = process.env.APP_URL || 'http://127.0.0.1:8765/';

const legacyBytes = Buffer.alloc(416);
legacyBytes[99 * 4] = 20;
const legacyDna = legacyBytes.toString('base64');
const ruler = `ruler_designer_1234567890={
    genes={
        hair_color={ 1 2 3 4 }
        gene_hair_type={ "hair_wavy" 100 "hair_curly" 50 }
        beards={ "beard_1" 0 "beard_2" 0 }
        mod_gene_x={ "option_a" 77 "option_b" 88 }
    }
}`;

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
await send('Page.navigate', { url: appUrl });
await wait(2000);
await evaluate('localStorage.clear(); location.reload(); true');
await wait(2000);

const steps = {};

async function addEntry(name, dna, hint = 'auto') {
  await evaluate(`document.querySelector('#new-button').click(); true`);
  await wait(300);
  await evaluate(`(() => {
    const nameInput = document.querySelector('#add-name');
    nameInput.value = ${JSON.stringify(name)};
    nameInput.dispatchEvent(new Event('input', { bubbles: true }));
    const format = document.querySelector('#add-format');
    format.value = ${JSON.stringify(hint)};
    format.dispatchEvent(new Event('change', { bubbles: true }));
    const area = document.querySelector('#add-dna');
    area.value = ${JSON.stringify(dna)};
    area.dispatchEvent(new Event('input', { bubbles: true }));
    return true;
  })()`);
  await wait(700);
  const validation = await evaluate(`({
    status: document.querySelector('#add-status')?.textContent,
    title: document.querySelector('#add-status')?.title,
    disabled: document.querySelector('#add-create')?.disabled,
  })`);
  await evaluate(`document.querySelector('#add-create').click(); true`);
  await wait(900);
  const created = await evaluate(`({
    title: document.querySelector('#detail-title')?.textContent,
    meta: document.querySelector('#meta')?.textContent,
    count: document.querySelector('#entry-count')?.textContent,
    ruler: document.querySelector('#ruler-code')?.value?.slice(0, 120),
    base64Length: document.querySelector('#base64-code')?.value?.length,
  })`);
  return { validation, created };
}

steps.legacy = await addEntry('Legacy Entry', legacyDna, 'auto');
steps.ruler = await addEntry('Ruler Entry', ruler, 'auto');

await evaluate(`document.querySelector('#tab-genes').click(); true`);
await wait(300);
steps.rulerGenes = await evaluate(`({
  extrasSection: [...document.querySelectorAll('.section-header')].map((item) => item.textContent.trim()).filter((text) => text.includes('Non-vanilla') || text.includes('genes') || text.includes('extra')),
  extrasRows: document.querySelectorAll('[data-extra-index]').length,
  sections: [...document.querySelectorAll('.section-header')].map((item) => item.textContent.trim()),
})`);

console.log(JSON.stringify(steps, null, 2));
console.log('ERRORS');
for (const error of errors) console.log(error);
ws.close();
process.exit(errors.length ? 1 : 0);
