const base = process.env.CDP_URL || 'http://127.0.0.1:9223';
const appUrl = process.env.APP_URL || 'http://127.0.0.1:8765/';
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

const support = await evaluate(`({
  showDirectoryPicker: typeof window.showDirectoryPicker,
  indexedDB: typeof indexedDB,
})`);

await evaluate(`(() => {
  const viewMenu = [...document.querySelectorAll('.menu')].find((menu) => menu.querySelector('.menu-button')?.textContent === 'Exibir' || menu.querySelector('.menu-button')?.textContent === 'View');
  viewMenu?.querySelector('.menu-button')?.click();
  return true;
})()`);
await wait(100);
const menuItems = await evaluate(`[...document.querySelectorAll('.menu.open .menu-item')].map((item) => item.textContent.trim())`);
await evaluate(`document.querySelectorAll('.menu').forEach((menu) => menu.classList.remove('open')); true`);

// Open the storage dialog via the File menu.
await evaluate(`(() => {
  const fileMenu = [...document.querySelectorAll('.menu')].find((menu) => {
    const text = menu.querySelector('.menu-button')?.textContent;
    return text === 'Arquivo' || text === 'File';
  });
  fileMenu?.querySelector('.menu-button')?.click();
  return true;
})()`);
await wait(100);
await evaluate(`(() => {
  const item = [...document.querySelectorAll('.menu.open .menu-item')].find((candidate) => candidate.textContent.includes('Storage') || candidate.textContent.includes('backup'));
  item?.click();
  return true;
})()`);
await wait(300);
const dialog = await evaluate(`({
  title: document.querySelector('.modal-backdrop .dialog-title')?.textContent,
  text: document.querySelector('.modal-backdrop .dialog-body')?.innerText,
  buttons: [...document.querySelectorAll('.modal-backdrop [data-storage]')].map((button) => ({ label: button.textContent.trim(), disabled: button.disabled })),
  startupSource: document.querySelector('#startup-source')?.value,
})`);

console.log(JSON.stringify({ support, menuItems, dialog }, null, 2));
console.log('ERRORS');
for (const error of errors) console.log(error);
ws.close();
process.exit(errors.length ? 1 : 0);
