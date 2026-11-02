import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(here, '..');
const translations = JSON.parse(fs.readFileSync(path.join(root, 'assets', 'translations.json'), 'utf8'));
const en = translations.strings.en;
const sources = [
  'index.html',
  'js/ui/app.js',
  'js/core/legacy-importer.js',
].map((file) => fs.readFileSync(path.join(root, file), 'utf8')).join('\n');

const keys = new Set();
for (const match of sources.matchAll(/(?:i18n\.t|\bt)\(\s*['"]([^'"]+)['"]/g)) keys.add(match[1]);
for (const match of sources.matchAll(/data-i18n(?:-placeholder)?="([^"]+)"/g)) keys.add(match[1]);
for (const match of sources.matchAll(/data-i18n="([^"]+)"/g)) keys.add(match[1]);

const missing = [...keys].filter((key) => !(key in en)).sort();
console.log('Missing keys:', missing.length ? missing.join('\n') : '(none)');
console.log('English keys:', Object.keys(en).length);
