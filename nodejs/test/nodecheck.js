// Функциональная проверка рантайма: core-модули, нативные аддоны, сеть, крипто.
// Всё локально (loopback) — интернет не нужен.
'use strict';
const assert = require('assert');

async function main() {
  // JSON
  assert.deepStrictEqual(JSON.parse('{"a":[1,2,3]}'), { a: [1, 2, 3] });

  // crypto (OpenSSL из бинаря node)
  const crypto = require('crypto');
  const hash = crypto.createHash('sha256').update('astra').digest('hex');
  assert.strictEqual(hash.length, 64);
  console.log('  crypto  : sha256 ok');

  // zlib (сжатие/распаковка)
  const zlib = require('zlib');
  const payload = Buffer.from('x'.repeat(1000));
  const packed = zlib.gzipSync(payload);
  assert.ok(packed.length < payload.length);
  assert.ok(zlib.gunzipSync(packed).equals(payload));
  console.log('  zlib    : gzip roundtrip ok');

  // fs (temp read/write)
  const fs = require('fs');
  const os = require('os');
  const path = require('path');
  const tmp = path.join(os.tmpdir(), 'nodecheck.txt');
  fs.writeFileSync(tmp, 'ok');
  assert.strictEqual(fs.readFileSync(tmp, 'utf8'), 'ok');
  fs.unlinkSync(tmp);
  console.log('  fs      : write/read ok');

  // http (loopback round-trip)
  const http = require('http');
  await new Promise((resolve, reject) => {
    const server = http.createServer((req, res) => res.end('pong'));
    server.listen(0, '127.0.0.1', () => {
      const { port } = server.address();
      http.get({ host: '127.0.0.1', port }, (res) => {
        let body = '';
        res.on('data', (c) => (body += c));
        res.on('end', () => {
          server.close();
          try {
            assert.strictEqual(body, 'pong');
            console.log('  http    : loopback ok');
            resolve();
          } catch (e) {
            reject(e);
          }
        });
      }).on('error', reject);
    });
  });

  console.log('nodecheck: OK');
}

main().catch((e) => {
  console.error('nodecheck FAIL:', e.message);
  process.exit(1);
});
