import test from 'node:test';
import assert from 'node:assert/strict';
import * as Folder from '../js/storage/folder-store.js';

function fakeDirectory() {
  const files = new Map();
  const dirs = new Map();

  const makeDir = (name) => ({
    name,
    async getDirectoryHandle(child, { create = false } = {}) {
      if (!dirs.has(child)) {
        if (!create) throw new Error('NotFound');
        dirs.set(child, makeDir(child));
      }
      return dirs.get(child);
    },
    async getFileHandle(child, { create = false } = {}) {
      if (!files.has(child)) {
        if (!create) throw new Error('NotFound');
        files.set(child, { name: child, text: '' });
      }
      const entry = files.get(child);
      return {
        name: child,
        async getFile() {
          return {
            name: child,
            async text() { return entry.text; },
            async arrayBuffer() { return new TextEncoder().encode(entry.text).buffer; },
          };
        },
        async createWritable() {
          let pending = '';
          return {
            async write(value) { pending = String(value); },
            async close() { entry.text = pending; },
          };
        },
      };
    },
  });

  const root = makeDir('mock');
  root.files = files;
  root.dirs = dirs;
  return root;
}

test('folder sync JSON round trip', async () => {
  const handle = fakeDirectory();
  const payload = { format: 'ck3-dna-manager-qt', version: 1, webSync: true, entries: [{ id: 'x', name: 'Test' }] };
  await Folder.writeSyncLibrary(handle, payload);
  const restored = await Folder.readSyncLibrary(handle);
  assert.deepEqual(restored, payload);
});

test('missing sync file returns null', async () => {
  const handle = fakeDirectory();
  assert.equal(await Folder.readSyncLibrary(handle), null);
});

test('find database file', async () => {
  const handle = fakeDirectory();
  handle.files.set('dna.db', { name: 'dna.db', text: 'SQLite format 3' });
  const result = await Folder.findDatabaseFile(handle);
  assert.equal(result.name, 'dna.db');
  assert.equal(await result.file.text(), 'SQLite format 3');
});
