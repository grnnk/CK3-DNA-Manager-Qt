// Web client entry point: load assets, initialize the UI, and start.
import { GeneCatalog } from '../core/catalog.js';
import { I18n } from '../core/i18n.js';
import { LocalStore } from '../storage/local-store.js';
import { state } from './state.js';
import { escapeHtml, setStatus } from './shared.js';
import { showWelcome, sortedEntries } from './library.js';
import { loadEntry } from './detail.js';
import { restoreFolder } from './folder.js';
import { buildMenus, buildLanguageSelect, applyTranslations } from './menus.js';
import { setupEvents } from './events.js';

async function boot() {
  const embedded = globalThis.__CK3DNA_ASSETS__;
  const base = new URL('./assets/', document.baseURI);
  const [genes, legacy, translations] = embedded
    ? [embedded.genes, embedded.legacy, embedded.translations]
    : await Promise.all([
      fetch(new URL('genes.json', base)).then((response) => response.json()),
      fetch(new URL('legacy_genes.json', base)).then((response) => response.json()),
      fetch(new URL('translations.json', base)).then((response) => response.json()),
    ]);

  state.catalog = GeneCatalog.fromJson(genes);
  state.legacyCatalog = GeneCatalog.fromJson(legacy);
  state.i18n = new I18n();
  state.i18n.load(translations);
  state.store = new LocalStore();

  const settings = state.store.getSettings();
  const systemLanguage = (navigator.language || 'en').slice(0, 2).toLowerCase();
  const language = state.i18n.availableLanguages().includes(settings.language)
    ? settings.language
    : (state.i18n.availableLanguages().includes(systemLanguage) ? systemLanguage : 'en');
  state.i18n.setLanguage(language);
  state.sort = ['updated', 'created', 'name'].includes(settings.sort) ? settings.sort : 'updated';
  state.startupSource = settings.startupSource === 'folder' ? 'folder' : 'local';

  state.entries = state.store.entries();
  await restoreFolder();
  state.entries = state.store.entries();
  buildMenus();
  buildLanguageSelect();
  applyTranslations();
  setupEvents();

  if (state.entries.length > 0) {
    const first = sortedEntries()[0];
    loadEntry(first.id);
  } else {
    showWelcome();
  }
  setStatus(state.i18n.t('status.ready'), 4000);
}

boot().catch((error) => {
  console.error(error);
  document.body.innerHTML = `<pre style="color:#e07b6a;padding:24px;white-space:pre-wrap">Failed to start CK3 DNA Manager:\n${escapeHtml(error.stack || error.message)}</pre>`;
});
