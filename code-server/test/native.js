// Нативные модули ломаются первыми, если сборка подтянула готовый .node с github
// или собралась под другой glibc.
const path = require("path")

const CS = "/usr/local/lib/code-server"
const VSCODE = path.join(CS, "lib/vscode/node_modules")

const modules = [
  [path.join(CS, "node_modules/argon2"), "argon2"],
  [path.join(VSCODE, "node-pty"), "node-pty"],
  [path.join(VSCODE, "@vscode/spdlog"), "@vscode/spdlog"],
  [path.join(VSCODE, "@vscode/sqlite3"), "@vscode/sqlite3"],
  [path.join(VSCODE, "@vscode/deviceid"), "@vscode/deviceid"],
  [path.join(VSCODE, "@parcel/watcher"), "@parcel/watcher"],
  [path.join(VSCODE, "kerberos"), "kerberos"],
]

let failed = 0
for (const [dir, name] of modules) {
  try {
    require(dir)
    console.log(`  ok    ${name}`)
  } catch (err) {
    failed++
    console.log(`  FAIL  ${name}: ${err.message}`)
  }
}

if (failed) {
  console.error(`${failed} нативных модулей не загрузились`)
  process.exit(1)
}
