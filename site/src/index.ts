import { join } from "path";
import { existsSync, statSync, openSync, readSync, closeSync } from "fs";

const PUBLIC = join(import.meta.dir, "../public");
const PORT = Number(process.env.PORT ?? 3001);

const MIME: Record<string, string> = {
  ".html": "text/html; charset=utf-8",
  ".css":  "text/css",
  ".js":   "application/javascript",
  ".mp4":  "video/mp4",
  ".jpg":  "image/jpeg",
  ".jpeg": "image/jpeg",
  ".png":  "image/png",
  ".txt":  "text/plain; charset=utf-8",
  ".sh":   "text/plain; charset=utf-8",
  ".json": "application/json",
};

Bun.serve({
  port: PORT,
  async fetch(req) {
    const url = new URL(req.url);
let pathname = url.pathname === "/" ? "/index.html" : url.pathname;
    const filePath = join(PUBLIC, pathname);

    if (!filePath.startsWith(PUBLIC)) return new Response("Forbidden", { status: 403 });
    if (!existsSync(filePath)) return new Response("Not Found", { status: 404 });

    const ext = "." + filePath.split(".").pop()!.toLowerCase();
    const contentType = MIME[ext] ?? "application/octet-stream";
    const total = statSync(filePath).size;
    const rangeHeader = req.headers.get("range");

    if (rangeHeader) {
      const match = rangeHeader.match(/bytes=(\d*)-(\d*)/);
      if (match) {
        const start = match[1] ? parseInt(match[1]) : 0;
        const end   = match[2] ? parseInt(match[2]) : Math.min(start + 2 * 1024 * 1024, total - 1);
        const chunkSize = end - start + 1;
        const buf = Buffer.allocUnsafe(chunkSize);
        const fd = openSync(filePath, "r");
        readSync(fd, buf, 0, chunkSize, start);
        closeSync(fd);
        return new Response(buf, {
          status: 206,
          headers: {
            "Content-Type": contentType,
            "Content-Range": `bytes ${start}-${end}/${total}`,
            "Content-Length": String(chunkSize),
            "Accept-Ranges": "bytes",
          },
        });
      }
    }

    return new Response(Bun.file(filePath), {
      headers: {
        "Content-Type": contentType,
        "Content-Length": String(total),
        "Accept-Ranges": "bytes",
      },
    });
  },
});

console.log(`http://localhost:${PORT}`);
