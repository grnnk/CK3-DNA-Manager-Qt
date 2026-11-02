// Optional File System Access API folder integration.
// The directory handle is remembered in IndexedDB; the browser can only
// re-open it automatically when the stored permission is still granted.

const IDB_NAME = 'ck3dna.v1.handles';
const IDB_STORE = 'handles';
const HANDLE_KEY = 'library-folder';

const DATABASE_NAMES = ['dna.db', 'database.db', 'library.db', 'ck3dna.db'];
const SYNC_DIRECTORY = 'web-sync';
const SYNC_FILE = 'library.json';

function openHandleDatabase() {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(IDB_NAME, 1);
    request.onupgradeneeded = () => {
      if (!request.result.objectStoreNames.contains(IDB_STORE)) {
        request.result.createObjectStore(IDB_STORE);
      }
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error || new Error('Could not open IndexedDB.'));
  });
}

function transaction(mode, callback) {
  return openHandleDatabase().then((database) => new Promise((resolve, reject) => {
    const tx = database.transaction(IDB_STORE, mode);
    const store = tx.objectStore(IDB_STORE);
    const request = callback(store);
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error || new Error('IndexedDB request failed.'));
    tx.oncomplete = () => database.close();
  }));
}

export function isSupported() {
  return typeof globalThis.showDirectoryPicker === 'function' && typeof indexedDB !== 'undefined';
}

export function saveDirectoryHandle(handle) {
  return transaction('readwrite', (store) => store.put(handle, HANDLE_KEY));
}

export function loadDirectoryHandle() {
  return transaction('readonly', (store) => store.get(HANDLE_KEY));
}

export function clearDirectoryHandle() {
  return transaction('readwrite', (store) => store.delete(HANDLE_KEY));
}

export async function queryPermission(handle, mode = 'readwrite') {
  if (!handle?.queryPermission) return 'granted';
  try {
    return await handle.queryPermission({ mode });
  } catch {
    return 'denied';
  }
}

export async function requestPermission(handle, mode = 'readwrite') {
  if (!handle?.requestPermission) return 'granted';
  try {
    return await handle.requestPermission({ mode });
  } catch {
    return 'denied';
  }
}

async function getDirectory(handle, path, create = false) {
  let directory = handle;
  for (const part of path) {
    directory = await directory.getDirectoryHandle(part, { create });
  }
  return directory;
}

async function getFile(handle, path, create = false) {
  const directory = await getDirectory(handle, path.slice(0, -1), create);
  return directory.getFileHandle(path[path.length - 1], { create });
}

export async function readSyncLibrary(handle) {
  try {
    const fileHandle = await getFile(handle, [SYNC_DIRECTORY, SYNC_FILE], false);
    const file = await fileHandle.getFile();
    const text = await file.text();
    return JSON.parse(text);
  } catch {
    return null;
  }
}

export async function writeSyncLibrary(handle, root) {
  const fileHandle = await getFile(handle, [SYNC_DIRECTORY, SYNC_FILE], true);
  const writable = await fileHandle.createWritable();
  try {
    await writable.write(JSON.stringify(root, null, 2));
  } finally {
    await writable.close();
  }
}

export async function findDatabaseFile(handle) {
  for (const name of DATABASE_NAMES) {
    try {
      const fileHandle = await handle.getFileHandle(name);
      const file = await fileHandle.getFile();
      return { name, file };
    } catch {
      // Try the next candidate.
    }
  }
  return null;
}

export async function readFileFromDirectory(handle, path) {
  try {
    const fileHandle = await getFile(handle, path, false);
    return await fileHandle.getFile();
  } catch {
    return null;
  }
}
