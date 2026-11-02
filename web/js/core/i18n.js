// Flat-key translations with English fallback, ported from src/core/I18n.cpp.

export class I18n {
  constructor() {
    this.languages = [];
    this.names = new Map();
    this.strings = new Map();
    this.current = 'en';
  }

  load(root) {
    this.languages = [];
    this.names = new Map();
    this.strings = new Map();

    if (!root || typeof root !== 'object') throw new Error('Translations are not a JSON object');

    if (Array.isArray(root.languages)) {
      for (const value of root.languages) {
        if (!value || typeof value !== 'object') continue;
        const code = typeof value.code === 'string' ? value.code : '';
        if (!code) continue;
        if (!this.languages.includes(code)) this.languages.push(code);
        const name = typeof value.nativeName === 'string' && value.nativeName
          ? value.nativeName
          : (typeof value.name === 'string' && value.name ? value.name : code);
        this.names.set(code, name);
      }
    }

    if (root.strings && typeof root.strings === 'object') {
      for (const [code, values] of Object.entries(root.strings)) {
        if (!values || typeof values !== 'object' || Array.isArray(values)) continue;
        const map = new Map();
        for (const [key, value] of Object.entries(values)) {
          if (typeof value === 'string') map.set(key, value);
        }
        this.strings.set(code, map);
      }
    }

    if (!this.strings.has('en')) throw new Error('Translations do not contain English strings');
    if (!this.languages.includes('en')) this.languages.unshift('en');
    if (!this.names.has('en')) this.names.set('en', 'English');
    if (!this.strings.has(this.current)) this.current = 'en';
  }

  availableLanguages() {
    return [...this.languages];
  }

  languageName(code) {
    return this.names.get(code) || code;
  }

  setLanguage(code) {
    if (code === this.current || !this.strings.has(code)) return false;
    this.current = code;
    return true;
  }

  value(key) {
    const current = this.strings.get(this.current);
    const english = this.strings.get('en');
    const value = current?.get(key) || english?.get(key);
    return value || key;
  }

  t(key, ...args) {
    let text = this.value(key);
    args.forEach((arg, index) => {
      text = text.split(`%${index + 1}`).join(String(arg));
    });
    return text;
  }

  locale() {
    return this.current;
  }
}
