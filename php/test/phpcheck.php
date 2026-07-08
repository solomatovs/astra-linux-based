<?php
/**
 * Проверка работоспособности собранного PHP и его расширений:
 * наличие расширений + функциональные тесты (компрессия, крипто, сеть, БД-драйверы, intl, gd).
 */
declare(strict_types=1);

$failures = [];

function check(string $name, callable $fn): void {
    global $failures;
    try {
        $fn();
        printf("  OK   %s\n", $name);
    } catch (\Throwable $e) {
        printf("  FAIL %s: %s\n", $name, $e->getMessage());
        $failures[] = $name;
    }
}

echo "== interpreter ==\n";
printf("  php        : %s\n", PHP_VERSION);
printf("  zend       : %s\n", zend_version());

// 1) наличие расширений (падают, если при сборке не было нужных dev-библиотек)
echo "== loaded extensions ==\n";
foreach ([
    'Core', 'standard', 'mbstring', 'openssl', 'curl', 'zlib', 'bz2', 'zip',
    'pdo_mysql', 'mysqli', 'pdo_pgsql', 'pgsql', 'bcmath', 'sockets', 'pcntl',
    'soap', 'exif', 'ftp', 'gd', 'intl', 'sodium', 'readline',
    'json', 'filter', 'hash', 'ctype', 'iconv', 'PDO',
] as $ext) {
    check("extension $ext", function () use ($ext) {
        if (!extension_loaded($ext)) {
            throw new \RuntimeException("not loaded");
        }
    });
}

// 2) компрессия — round-trip (zlib/gzip/bz2)
echo "== compression ==\n";
$data = str_repeat("hello astra-linux ", 2000);
check("zlib (gzcompress) round-trip", function () use ($data) {
    if (gzuncompress(gzcompress($data, 9)) !== $data) throw new \RuntimeException("mismatch");
});
check("gzip (gzencode) round-trip", function () use ($data) {
    if (gzdecode(gzencode($data, 9)) !== $data) throw new \RuntimeException("mismatch");
});
check("bz2 round-trip", function () use ($data) {
    if (bzdecompress(bzcompress($data, 9)) !== $data) throw new \RuntimeException("mismatch");
});
check("zip write + read (ext/zip)", function () use ($data) {
    $f = tempnam(sys_get_temp_dir(), 'z') . '.zip';
    $z = new ZipArchive();
    if ($z->open($f, ZipArchive::CREATE | ZipArchive::OVERWRITE) !== true)
        throw new \RuntimeException("open for write failed");
    $z->addFromString('a.txt', $data);
    $z->close();
    $r = new ZipArchive();
    if ($r->open($f) !== true) throw new \RuntimeException("open for read failed");
    $got = $r->getFromName('a.txt');
    $r->close();
    unlink($f);
    if ($got !== $data) throw new \RuntimeException("content mismatch");
    $ver = defined('ZipArchive::LIBZIP_VERSION') ? ZipArchive::LIBZIP_VERSION : '?';
    printf("       libzip %s\n", $ver);
});

// 3) hash / openssl
echo "== hashing / crypto ==\n";
check("sha256 known vector", function () {
    if (hash('sha256', 'abc') !== 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
        throw new \RuntimeException("bad digest");
});
check("openssl version", function () {
    printf("       %s\n", OPENSSL_VERSION_TEXT);
});
check("openssl random + aes-256-cbc round-trip", function () {
    $key = openssl_random_pseudo_bytes(32);
    $iv  = openssl_random_pseudo_bytes(16);
    $ct  = openssl_encrypt('secret', 'aes-256-cbc', $key, OPENSSL_RAW_DATA, $iv);
    $pt  = openssl_decrypt($ct, 'aes-256-cbc', $key, OPENSSL_RAW_DATA, $iv);
    if ($pt !== 'secret') throw new \RuntimeException("mismatch");
});
check("password_hash bcrypt", function () {
    $h = password_hash('pw', PASSWORD_BCRYPT);
    if (!password_verify('pw', $h)) throw new \RuntimeException("verify failed");
});
check("password_hash argon2id (via sodium)", function () {
    // php собран без --with-password-argon2 (системный libargon2 стар),
    // но argon2id доступен через ext/sodium: PASSWORD_ARGON2ID определён и работает
    if (!defined('PASSWORD_ARGON2ID')) throw new \RuntimeException("no PASSWORD_ARGON2ID");
    $h = password_hash('pw', PASSWORD_ARGON2ID);
    if (!password_verify('pw', $h)) throw new \RuntimeException("verify failed");
    if (password_get_info($h)['algoName'] !== 'argon2id') throw new \RuntimeException("wrong algo");
});
check("sodium argon2id (crypto_pwhash_str)", function () {
    // прямой вызов argon2id из ext/sodium (та же реализация, что и в password_hash)
    $h = sodium_crypto_pwhash_str(
        'pw',
        SODIUM_CRYPTO_PWHASH_OPSLIMIT_INTERACTIVE,
        SODIUM_CRYPTO_PWHASH_MEMLIMIT_INTERACTIVE
    );
    if (!sodium_crypto_pwhash_str_verify($h, 'pw')) throw new \RuntimeException("verify failed");
});
check("sodium crypto_box roundtrip", function () {
    $kp = sodium_crypto_box_keypair();
    $pk = sodium_crypto_box_publickey($kp);
    $sk = sodium_crypto_box_secretkey($kp);
    $nonce = random_bytes(SODIUM_CRYPTO_BOX_NONCEBYTES);
    $c = sodium_crypto_box('msg', $nonce, sodium_crypto_box_keypair_from_secretkey_and_publickey($sk, $pk));
    $m = sodium_crypto_box_open($c, $nonce, sodium_crypto_box_keypair_from_secretkey_and_publickey($sk, $pk));
    if ($m !== 'msg') throw new \RuntimeException("mismatch");
});

// 4) сеть — loopback TCP (без интернета)
echo "== network ==\n";
check("TCP send/recv over 127.0.0.1", function () {
    $srv = stream_socket_server("tcp://127.0.0.1:0", $errno, $errstr);
    if (!$srv) throw new \RuntimeException("server: $errstr");
    $name = stream_socket_get_name($srv, false);
    $port = (int) substr($name, strrpos($name, ':') + 1);
    $cli = stream_socket_client("tcp://127.0.0.1:$port", $e, $s, 5);
    if (!$cli) throw new \RuntimeException("client: $s");
    $conn = stream_socket_accept($srv, 5);
    fwrite($cli, "ping");
    if (fread($conn, 4) !== "ping") throw new \RuntimeException("recv mismatch");
    fclose($cli); fclose($conn); fclose($srv);
});
check("gethostbyname localhost", function () {
    if (gethostbyname('localhost') === 'localhost') throw new \RuntimeException("resolve failed");
});

// 5) SQLite через PDO
echo "== pdo sqlite ==\n";
check("in-memory db + query", function () {
    if (!extension_loaded('pdo_sqlite')) { echo "       (pdo_sqlite не собран — пропуск)\n"; return; }
    $db = new PDO('sqlite::memory:');
    $db->exec('create table t(x int)');
    $st = $db->prepare('insert into t values(?)');
    for ($i = 0; $i < 10; $i++) $st->execute([$i]);
    $sum = (int) $db->query('select sum(x) from t')->fetchColumn();
    if ($sum !== 45) throw new \RuntimeException("bad sum: $sum");
});

// 6) intl / mbstring
echo "== i18n ==\n";
check("mbstring strlen (utf-8)", function () {
    if (mb_strlen('привет', 'UTF-8') !== 6) throw new \RuntimeException("bad length");
});
check("intl collator", function () {
    $c = new Collator('ru_RU');
    $arr = ['б', 'а', 'в'];
    $c->sort($arr);
    if ($arr !== ['а', 'б', 'в']) throw new \RuntimeException("bad sort");
});

// 7) gd
echo "== gd ==\n";
check("gd create + png", function () {
    $im = imagecreatetruecolor(4, 4);
    ob_start(); imagepng($im); $png = ob_get_clean();
    imagedestroy($im);
    if (substr($png, 1, 3) !== 'PNG') throw new \RuntimeException("bad png header");
});

// 8) прочее
echo "== misc ==\n";
check("json round-trip", function () {
    $x = json_decode(json_encode(['a' => [1, 2, 3]]), true);
    if ($x['a'] !== [1, 2, 3]) throw new \RuntimeException("mismatch");
});
check("bcmath", function () {
    if (bcadd('0.1', '0.2', 1) !== '0.3') throw new \RuntimeException("mismatch");
});

echo "\n";
if ($failures) {
    printf("RESULT: FAIL (%d): %s\n", count($failures), implode(', ', $failures));
    exit(1);
}
echo "RESULT: ALL OK\n";
