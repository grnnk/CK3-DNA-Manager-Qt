// localStorage-backed library repository.

const LIBRARY_KEY = 'ck3dna.v1.library';
const SETTINGS_KEY = 'ck3dna.v1.settings';
const PORTRAIT_PREFIX = 'ck3dna.v1.portrait.';
const THUMB_PREFIX = 'ck3dna.v1.thumb.';

function newId() {
  if (globalThis.crypto?.randomUUID) return crypto.randomUUID();
  return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, (character) => {
    const random = Math.floor(Math.random() * 16);
    const value = character === 'x' ? random : ((random & 0x3) | 0x8);
    return value.toString(16);
  });
}

function emptyLibrary() {
  return { version: 1, entries: [] };
}

export class LocalStore {
  constructor(storage = globalThis.localStorage) {
    this.storage = storage;
  }

  load() {
    const raw = this.storage.getItem(LIBRARY_KEY);
    if (!raw) return { library: emptyLibrary(), error: '' };
    try {
      const parsed = JSON.parse(raw);
      if (!parsed || !Array.isArray(parsed.entries)) throw new Error('Invalid library shape');
      return { library: { version: 1, entries: parsed.entries }, error: '' };
    } catch (error) {
      return { library: emptyLibrary(), error: error.message || 'The library could not be read.' };
    }
  }

  save(library) {
    const payload = JSON.stringify({ version: 1, entries: library.entries });
    this.storage.setItem(LIBRARY_KEY, payload);
  }

  entries() {
    return this.load().library.entries;
  }

  get(id) {
    return this.entries().find((entry) => entry.id === id) || null;
  }

  insert(entry) {
    const library = this.load().library;
    const now = Date.now();
    const record = {
      id: entry.id || newId(),
      name: (entry.name || '').trim() || 'Untitled',
      notes: entry.notes || '',
      dna: entry.dna || '',
      rulerMeta: entry.rulerMeta || '',
      favorite: Boolean(entry.favorite),
      createdAt: entry.createdAt > 0 ? entry.createdAt : now,
      updatedAt: entry.updatedAt > 0 ? entry.updatedAt : now,
      hasPortrait: Boolean(entry.hasPortrait),
    };
    if (!record.dna) throw new Error('Cannot save an empty DNA payload.');
    library.entries.push(record);
    this.save(library);
    return record;
  }

  update(entry, { preserveTimestamp = false } = {}) {
    const library = this.load().library;
    const index = library.entries.findIndex((candidate) => candidate.id === entry.id);
    if (index < 0) throw new Error('The DNA entry no longer exists.');
    const current = library.entries[index];
    library.entries[index] = {
      ...current,
      ...entry,
      name: (entry.name || '').trim() || 'Untitled',
      updatedAt: preserveTimestamp && entry.updatedAt > 0 ? entry.updatedAt : Date.now(),
    };
    this.save(library);
    return library.entries[index];
  }

  remove(id) {
    const library = this.load().library;
    library.entries = library.entries.filter((entry) => entry.id !== id);
    this.save(library);
    this.removePortrait(id);
  }

  getPortrait(id) {
    return this.storage.getItem(`${PORTRAIT_PREFIX}${id}`);
  }

  getThumbnail(id) {
    return this.storage.getItem(`${THUMB_PREFIX}${id}`);
  }

  setPortrait(id, portraitDataUrl, thumbnailDataUrl) {
    const portraitKey = `${PORTRAIT_PREFIX}${id}`;
    const thumbKey = `${THUMB_PREFIX}${id}`;
    const previousPortrait = this.storage.getItem(portraitKey);
    const previousThumb = this.storage.getItem(thumbKey);
    const thumb = thumbnailDataUrl || portraitDataUrl;
    try {
      this.storage.setItem(portraitKey, portraitDataUrl);
      this.storage.setItem(thumbKey, thumb);
    } catch (error) {
      if (previousPortrait === null) this.storage.removeItem(portraitKey);
      else this.storage.setItem(portraitKey, previousPortrait);
      if (previousThumb === null) this.storage.removeItem(thumbKey);
      else this.storage.setItem(thumbKey, previousThumb);
      throw error;
    }
  }

  removePortrait(id) {
    this.storage.removeItem(`${PORTRAIT_PREFIX}${id}`);
    this.storage.removeItem(`${THUMB_PREFIX}${id}`);
  }

  getSettings() {
    const raw = this.storage.getItem(SETTINGS_KEY);
    if (!raw) return { language: '', sort: 'updated', startupSource: 'local' };
    try {
      const parsed = JSON.parse(raw);
      return {
        language: typeof parsed.language === 'string' ? parsed.language : '',
        sort: typeof parsed.sort === 'string' ? parsed.sort : 'updated',
        startupSource: parsed.startupSource === 'folder' ? 'folder' : 'local',
      };
    } catch {
      return { language: '', sort: 'updated', startupSource: 'local' };
    }
  }

  saveSettings(settings) {
    this.storage.setItem(SETTINGS_KEY, JSON.stringify(settings));
  }

  usageBytes() {
    let total = 0;
    for (let i = 0; i < this.storage.length; i += 1) {
      const key = this.storage.key(i);
      if (!key || !key.startsWith('ck3dna.v1.')) continue;
      total += key.length + (this.storage.getItem(key)?.length || 0);
    }
    return total * 2; // UTF-16 estimate.
  }

  clear() {
    const keys = [];
    for (let i = 0; i < this.storage.length; i += 1) {
      const key = this.storage.key(i);
      if (key && key.startsWith('ck3dna.v1.')) keys.push(key);
    }
    for (const key of keys) this.storage.removeItem(key);
  }
}

export function entryToExport(entry, { portraitPng = '', geneCount = 0, moddedGeneCount = 0, rulerDesigner = '' } = {}) {
  const object = {
    name: entry.name,
    notes: entry.notes || '',
    favorite: Boolean(entry.favorite),
  };
  if (entry.rulerMeta) object.rulerMeta = entry.rulerMeta;
  object.base64 = entry.dna;
  object.geneCount = geneCount;
  object.moddedGeneCount = moddedGeneCount;
  if (rulerDesigner) object.rulerDesigner = rulerDesigner;
  object.createdAt = entry.createdAt;
  object.updatedAt = entry.updatedAt;
  if (portraitPng) object.portraitPng = portraitPng;
  return object;
}

export function buildExport(entries) {
  return {
    format: 'ck3-dna-manager-qt',
    version: 1,
    exportedAt: new Date().toISOString(),
    entries,
  };
}

export { newId };
