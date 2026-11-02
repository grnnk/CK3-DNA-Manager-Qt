// Minimal CDP smoke test for the static app. Requires Edge/Chrome with --remote-debugging-port.
const base = process.env.CDP_URL || 'http://127.0.0.1:9223';
const appUrl = process.env.APP_URL || 'http://127.0.0.1:8765/';

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
    errors.push(message.params.exceptionDetails?.exception?.description
      || message.params.exceptionDetails?.text
      || 'Unknown exception');
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

async function evaluate(expression, awaitPromise = true) {
  const result = await send('Runtime.evaluate', {
    expression,
    awaitPromise,
    returnByValue: true,
  });
  if (result.exceptionDetails) {
    throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
  }
  return result.result.value;
}

await send('Runtime.enable');
await send('Log.enable');
await send('Page.enable');
await send('Page.navigate', { url: appUrl });
await new Promise((resolve) => setTimeout(resolve, 2500));

const summary = await evaluate(`(() => {
  const text = (selector) => document.querySelector(selector)?.textContent?.trim() || '';
  return {
    title: document.title,
    welcomeHidden: document.querySelector('#welcome')?.hidden,
    contentHidden: document.querySelector('#content')?.hidden,
    status: text('#status-text'),
    entryCount: text('#entry-count'),
    menuButtons: [...document.querySelectorAll('.menu-button')].map((item) => item.textContent),
    languageOptions: [...document.querySelectorAll('#language option')].map((item) => item.textContent),
    geneCategoryOptions: [...document.querySelectorAll('#gene-category option')].map((item) => item.textContent),
    bodyStart: document.body.innerText.slice(0, 400),
  };
})()`);

console.log(JSON.stringify(summary, null, 2));
console.log('LOGS');
for (const log of logs.slice(0, 30)) console.log(log);
console.log('ERRORS');
for (const error of errors) console.log(error);
ws.close();
process.exit(errors.length ? 1 : 0);
