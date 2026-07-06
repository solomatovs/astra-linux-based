const sum = Array.from({ length: 10 }, (_, i) => i + 1).reduce((a, b) => a + b, 0);
console.log(`hello from node ${process.version}, sum(1..=10) = ${sum}`);
if (sum !== 55) {
  console.error(`FAIL: expected 55, got ${sum}`);
  process.exit(1);
}
