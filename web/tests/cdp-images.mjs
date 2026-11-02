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

const summary = await evaluate(`(async () => {
  const Images = await import('./js/core/images.js');
  const sourceCanvas = document.createElement('canvas');
  sourceCanvas.width = 100;
  sourceCanvas.height = 80;
  const sourceContext = sourceCanvas.getContext('2d');
  sourceContext.fillStyle = '#ff0000';
  sourceContext.fillRect(0, 0, 100, 80);
  const png = await new Promise((resolve) => sourceCanvas.toBlob(resolve, 'image/png'));
  const image = await Images.loadImage(png);
  const thumbBlob = await Images.makeThumbnail(image, 96, 120);
  const thumbUrl = await Images.blobToDataUrl(thumbBlob);
  const thumb = await Images.loadImage(thumbUrl);
  const canvas = document.createElement('canvas');
  canvas.width = 96;
  canvas.height = 120;
  const context = canvas.getContext('2d');
  context.drawImage(thumb, 0, 0);
  const pixel = context.getImageData(48, 60, 1, 1).data;
  return {
    source: { width: image.width || image.naturalWidth, height: image.height || image.naturalHeight },
    thumbnail: { width: thumb.width || thumb.naturalWidth, height: thumb.height || thumb.naturalHeight },
    centerPixel: [...pixel],
    type: thumbBlob.type,
    size: thumbBlob.size,
  };
})()`);

console.log(JSON.stringify(summary, null, 2));
console.log('ERRORS');
for (const error of errors) console.log(error);
ws.close();
process.exit(errors.length ? 1 : 0);
