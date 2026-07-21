#!/usr/bin/env node
// Готовит dist/index.html апстрима (@musistudio/claude-code-router) к офлайн-работе:
//
//   1. Оставляет в инлайновых @font-face только woff2.
//      Апстрим с 2.0.0 инлайнит шрифт remixicon сразу пятью форматами в base64 —
//      eot (дважды), woff2, woff, ttf. Это шаблон "bulletproof @font-face" эпохи IE8:
//      браузер обязан скачать все ~2.9 МБ, хотя использует только woff2 (~240 КБ).
//      base64 раздувает данные на треть и почти не жмётся gzip"ом, а инлайн в HTML
//      делает всё это блокирующим рендер. woff2 понимают все браузеры с 2016 года,
//      а UI и так грузится как ES-модуль, так что остальные форматы недостижимы.
//
//   2. Переписывает внешние CDN-ссылки на локальные пути.
//      Апстрим в рантайме ходит в интернет за monaco (jsdelivr), providers.json (r2.dev)
//      и combo.css (nerdfonts). Файлы вендорятся в образ рядом с index.html, а CCR
//      отдаёт всю папку dist через @fastify/static с prefix=/ui/ — поэтому пути тут
//      абсолютные (/ui/...), а не относительные: AMD-загрузчик monaco резолвит
//      paths.vs в том числе из web-worker"ов, где база документа не действует.
//
// Скрипт идемпотентен: повторный запуск ничего не меняет.

const fs = require("fs");

const file = process.argv[2];
if (!file) {
  console.error("usage: optimize-ui.js <index.html>");
  process.exit(1);
}

let html = fs.readFileSync(file, "utf8");
const before = Buffer.byteLength(html);

// ---------- 1. шрифты ----------

// data:-URI и относительные пути не содержат ")" и кавычек — граница надёжна
const ENTRY = /url\(\s*([\x27\"]?)([^)\x27\"]+)\1\s*\)(?:\s*format\(\s*([\x27\"]?)([^)\x27\"]+)\3\s*\))?/g;
// весь каскад src (их бывает несколько подряд — тот самый eot-хак) вместе с ";"
const SRC_CASCADE = /src\s*:\s*(?:url\([^)]*\)(?:\s*format\([^)]*\))?\s*,?\s*)+;?/g;

let faces = 0;
let droppedSrc = 0;

html = html.replace(/@font-face\s*\{[^}]*\}/g, (face) => {
  const entries = [...face.matchAll(ENTRY)];
  const woff2 = entries.find(
    (e) => /woff2/.test(e[4] || "") || /^data:font\/woff2/.test(e[2])
  );
  // нет woff2 либо уже единственный источник — оставляем как есть
  if (!woff2 || entries.length < 2) return face;

  faces++;
  droppedSrc += entries.length - 1;

  const rest = face.replace(SRC_CASCADE, "").replace(/@font-face\s*\{/, "");
  return `@font-face{src:url(${woff2[2]}) format(\"woff2\");${rest}`;
});

// ---------- 2. внешние ссылки ----------

// Версия monaco в URL меняется от версии апстрима (1.0.73 -> 0.52.2, 2.0.0 -> 0.55.1),
// поэтому матчим по шаблону, а не по конкретной версии; тарбол кладём в /ui/vs.
// Аналогично bucket-id у r2.dev — не константа апстрима.
const REWRITES = [
  { what: "monaco",       re: /https:\/\/cdn\.jsdelivr\.net\/npm\/monaco-editor@[0-9][^\"\x27]*\/min\/vs/g, to: "/ui/vs" },
  { what: "providers",    re: /https:\/\/pub-[a-z0-9]+\.r2\.dev\/providers\.json/g,                         to: "/ui/providers.json" },
  { what: "nerdfonts",    re: /https:\/\/www\.nerdfonts\.com\/assets\/css\/combo\.css/g,                    to: "/ui/combo.css" },
];

const rewritten = [];
for (const r of REWRITES) {
  const n = (html.match(r.re) || []).length;
  if (n > 0) {
    html = html.replace(r.re, r.to);
    rewritten.push(`${r.what}:${n}`);
  }
}

// ---------- итог ----------

if (faces === 0 && rewritten.length === 0) {
  // Штатно для версий без инлайна шрифта и без CDN-ссылок.
  console.log("optimize-ui: нечего оптимизировать — пропускаю");
  process.exit(0);
}

fs.writeFileSync(file, html);
const after = Buffer.byteLength(html);

console.log(
  `optimize-ui: @font-face: ${faces} (выброшено src: ${droppedSrc}), ` +
    `ссылки -> локальные: ${rewritten.join(", ") || "нет"}, ` +
    `${(before / 1048576).toFixed(2)} МБ -> ${(after / 1048576).toFixed(2)} МБ`
);

// Апстрим может добавить новые внешние зависимости при обновлении версии — тогда UI
// снова полезет в интернет. Пусть это будет видно в логе сборки, а не в проде.
const leftover = [...new Set(
  (html.match(/https:\/\/[a-z0-9.-]+\/[^\"\x27\x60\s)]*/gi) || [])
    .filter((u) => /jsdelivr|unpkg|cdnjs|r2\.dev|nerdfonts|googleapis|jsdmirror/i.test(u))
)];
if (leftover.length) {
  console.warn("optimize-ui: ВНИМАНИЕ, остались внешние ссылки:\n  " + leftover.join("\n  "));
}
