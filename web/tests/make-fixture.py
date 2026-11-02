import sqlite3, base64, os
path = os.path.join('web','tests','fixtures','test.db')
if os.path.exists(path): os.remove(path)
conn = sqlite3.connect(path)
conn.execute('''CREATE TABLE entries (
  id TEXT PRIMARY KEY, name TEXT NOT NULL, notes TEXT NOT NULL DEFAULT '',
  dna BLOB NOT NULL, portrait BLOB, thumbnail BLOB, favorite INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL,
  ruler_meta TEXT NOT NULL DEFAULT ''
)''')
dna = bytearray(416)
dna[0] = 10; dna[1] = 20; dna[2] = 30; dna[3] = 40
# slot 99 hair type, slot 100 baldness remain 0
# 1x1 red PNG
png = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAFgwJ/lK3P9wAAAABJRU5ErkJggg==')
conn.execute('INSERT INTO entries VALUES (?,?,?,?,?,?,?,?,?,?)', (
  'db-test-1', 'DB Test Entry', 'Imported from dna.db', bytes(dna), png, png, 1,
  1600000000000, 1600000001000, ''
))
conn.commit(); conn.close()
print(os.path.getsize(path))
