// Ждём, пока code-server поднимется, и проверяем, что он отдаёт workbench.
const http = require("http")

const TARGET = process.env.SMOKE_URL || "http://127.0.0.1:8080/"
const TIMEOUT_MS = 90_000
const deadline = Date.now() + TIMEOUT_MS

const get = (url, redirects = 5) =>
  new Promise((resolve, reject) => {
    const req = http.get(url, (res) => {
      // корень редиректит на ./?folder=<workspace>
      if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location && redirects > 0) {
        res.resume()
        resolve(get(new URL(res.headers.location, url).toString(), redirects - 1))
        return
      }
      let body = ""
      res.setEncoding("utf8")
      res.on("data", (chunk) => (body += chunk))
      res.on("end", () => resolve({ status: res.statusCode, body }))
    })
    req.on("error", reject)
    req.setTimeout(5000, () => req.destroy(new Error("timeout")))
  })

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

async function main() {
  let last
  while (Date.now() < deadline) {
    try {
      last = await get(TARGET)
      if (last.status === 200) break
    } catch (err) {
      last = { status: 0, body: err.message }
    }
    await sleep(1000)
  }

  if (!last || last.status !== 200) {
    console.error(`code-server не ответил 200 за ${TIMEOUT_MS / 1000}s: ${last && last.status} ${last && last.body}`)
    process.exit(1)
  }

  if (!/workbench|code-server/i.test(last.body)) {
    console.error("ответ 200, но это не страница workbench:\n" + last.body.slice(0, 500))
    process.exit(1)
  }

  console.log(`  ok    GET ${TARGET} -> 200, ${last.body.length} байт`)
}

main()
