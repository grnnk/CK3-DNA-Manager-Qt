// Browser image helpers: portrait normalization, thumbnails, clipboard, downloads.

export const IMAGE_EXTENSIONS = ['png', 'jpg', 'jpeg', 'jfif', 'webp', 'bmp', 'gif'];

export function isImageFile(file) {
  if (!file) return false;
  if (file.type && file.type.startsWith('image/')) return true;
  const name = (file.name || '').toLowerCase();
  return IMAGE_EXTENSIONS.some((extension) => name.endsWith(`.${extension}`));
}

export async function loadImage(source) {
  if (typeof createImageBitmap === 'function') {
    try {
      return await createImageBitmap(source, { imageOrientation: 'from-image' });
    } catch {
      // Fall through to the HTMLImageElement path.
    }
  }

  return new Promise((resolve, reject) => {
    const url = typeof source === 'string' ? source : URL.createObjectURL(source);
    const image = new Image();
    image.onload = () => {
      if (typeof source !== 'string') URL.revokeObjectURL(url);
      resolve(image);
    };
    image.onerror = () => {
      if (typeof source !== 'string') URL.revokeObjectURL(url);
      reject(new Error('The image could not be loaded.'));
    };
    image.src = url;
  });
}

function imageSize(image) {
  return {
    width: image.width || image.naturalWidth || 0,
    height: image.height || image.naturalHeight || 0,
  };
}

function makeCanvas(width, height) {
  const canvas = document.createElement('canvas');
  canvas.width = Math.max(1, Math.round(width));
  canvas.height = Math.max(1, Math.round(height));
  return canvas;
}

function canvasToBlob(canvas, type, quality) {
  return new Promise((resolve) => {
    canvas.toBlob((blob) => resolve(blob), type, quality);
  });
}

async function encodeCanvas(canvas, preferredType = 'image/webp', quality = 0.88) {
  let blob = await canvasToBlob(canvas, preferredType, quality);
  if (!blob && preferredType !== 'image/png') {
    blob = await canvasToBlob(canvas, 'image/png');
  }
  return blob;
}

export async function encodePortrait(image, maxSize = 1600) {
  const { width, height } = imageSize(image);
  if (!width || !height) throw new Error('The image has no pixels.');

  const scale = Math.min(1, maxSize / Math.max(width, height));
  const canvas = makeCanvas(width * scale, height * scale);
  const context = canvas.getContext('2d');
  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = 'high';
  context.drawImage(image, 0, 0, canvas.width, canvas.height);
  return encodeCanvas(canvas, 'image/webp', 0.86);
}

export async function makeThumbnail(image, targetWidth = 96, targetHeight = 120) {
  const { width, height } = imageSize(image);
  if (!width || !height) return null;

  const scale = Math.max(targetWidth / width, targetHeight / height);
  const sourceWidth = targetWidth / scale;
  const sourceHeight = targetHeight / scale;
  const sourceX = (width - sourceWidth) / 2;
  const sourceY = (height - sourceHeight) / 2;

  const canvas = makeCanvas(targetWidth, targetHeight);
  const context = canvas.getContext('2d');
  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = 'high';
  context.drawImage(
    image,
    sourceX,
    sourceY,
    sourceWidth,
    sourceHeight,
    0,
    0,
    targetWidth,
    targetHeight,
  );
  return encodeCanvas(canvas, 'image/webp', 0.9);
}

export function blobToDataUrl(blob) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(String(reader.result));
    reader.onerror = () => reject(reader.error || new Error('Could not read the image.'));
    reader.readAsDataURL(blob);
  });
}

export function dataUrlToBlob(dataUrl) {
  const [header, payload] = dataUrl.split(',');
  const mimeMatch = /data:([^;]+)/.exec(header || '');
  const mime = mimeMatch ? mimeMatch[1] : 'application/octet-stream';
  const binary = atob(payload || '');
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i += 1) bytes[i] = binary.charCodeAt(i);
  return new Blob([bytes], { type: mime });
}

export function blobToBase64(blob) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => {
      const result = String(reader.result || '');
      resolve(result.includes(',') ? result.split(',')[1] : result);
    };
    reader.onerror = () => reject(reader.error || new Error('Could not read the image.'));
    reader.readAsDataURL(blob);
  });
}

export function base64ToBlob(base64, mime = 'image/png') {
  const binary = atob(base64);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i += 1) bytes[i] = binary.charCodeAt(i);
  return new Blob([bytes], { type: mime });
}

export async function dataUrlToPngBase64(dataUrl) {
  const image = await loadImage(dataUrl);
  const { width, height } = imageSize(image);
  const canvas = makeCanvas(width, height);
  canvas.getContext('2d').drawImage(image, 0, 0);
  return canvas.toDataURL('image/png').split(',')[1];
}

export function imageFromClipboardEvent(event) {
  const items = event.clipboardData?.items;
  if (!items) return null;
  for (const item of items) {
    if (item.type && item.type.startsWith('image/')) {
      const file = item.getAsFile();
      if (file) return file;
    }
  }
  return null;
}

export function clipboardEventHasText(event) {
  const items = event.clipboardData?.items;
  if (!items) return false;
  for (const item of items) {
    if (item.type === 'text/plain' || item.type === 'text/html') return true;
  }
  return false;
}

export async function readClipboardImage() {
  if (!navigator.clipboard?.read) return null;
  try {
    const items = await navigator.clipboard.read();
    for (const item of items) {
      const type = item.types.find((candidate) => candidate.startsWith('image/'));
      if (type) return item.getType(type);
    }
  } catch {
    return null;
  }
  return null;
}

export function downloadBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = filename;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export function downloadText(text, filename, type = 'application/json') {
  downloadBlob(new Blob([text], { type }), filename);
}
