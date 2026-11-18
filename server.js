/**
 * Smart Route Planner - Backend Server (Node.js)
 * Supports Express when installed, with seamless zero-dependency fallback to Node's native HTTP.
 * 
 * Endpoints:
 *   POST /api/route - Invokes compiled C route_cli.exe with query parameters
 *   GET  /api/map   - Returns map_data.json
 *   GET  /          - Serves the interactive web interface from public/
 */

const http = require('http');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const PORT = process.env.PORT || 3000;
const BINARY_NAME = process.platform === 'win32' ? 'route_cli.exe' : 'route_cli';
const BINARY_PATH = path.join(__dirname, BINARY_NAME);
const MAP_FILE_PATH = path.join(__dirname, 'map_data.json');
const PUBLIC_DIR = path.join(__dirname, 'public');

// MIME types for static file serving
const MIME_TYPES = {
    '.html': 'text/html; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.js': 'application/javascript; charset=utf-8',
    '.json': 'application/json; charset=utf-8',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon'
};

/**
 * Executes the compiled C binary and returns parsed JSON output.
 * @param {Object} query - Route query object
 * @returns {Promise<Object>}
 */
function executeRoutingEngine(query) {
    return new Promise((resolve, reject) => {
        if (!fs.existsSync(BINARY_PATH)) {
            return reject(new Error(`Compiled C binary not found at ${BINARY_PATH}. Run 'make all' first.`));
        }

        const stdinPayload = JSON.stringify({
            source: Number(query.source),
            destination: Number(query.destination),
            mode: query.mode || query.criterion || 'time',
            waypoints: Array.isArray(query.waypoints) ? query.waypoints.map(Number) : [],
            alternatives: Boolean(query.alternatives),
            traffic_updates: Array.isArray(query.traffic_updates) ? query.traffic_updates : [],
            map_file: MAP_FILE_PATH.replace(/\\/g, '/')
        });

        const child = spawn(BINARY_PATH, [], {
            cwd: __dirname,
            stdio: ['pipe', 'pipe', 'pipe']
        });

        let stdoutData = '';
        let stderrData = '';

        child.stdout.on('data', (chunk) => {
            stdoutData += chunk.toString();
        });

        child.stderr.on('data', (chunk) => {
            stderrData += chunk.toString();
        });

        child.on('error', (err) => {
            reject(new Error(`Failed to spawn C routing binary: ${err.message}`));
        });

        child.on('close', (code) => {
            if (stderrData.trim()) {
                console.warn('[C Engine stderr]:', stderrData.trim());
            }

            try {
                const parsed = JSON.parse(stdoutData.trim());
                resolve(parsed);
            } catch (parseErr) {
                console.error('[C Engine stdout parse error]:', stdoutData);
                reject(new Error(`C binary output parse error: ${parseErr.message}. Output was: ${stdoutData.slice(0, 300)}`));
            }
        });

        child.stdin.write(stdinPayload);
        child.stdin.end();
    });
}

/**
 * Handle incoming HTTP requests
 */
function handleRequest(req, res) {
    const parsedUrl = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
    const pathname = parsedUrl.pathname;

    // Enable CORS for frontend integration
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    if (req.method === 'OPTIONS') {
        res.writeHead(204);
        res.end();
        return;
    }

    // Endpoint: POST /api/route
    if (pathname === '/api/route' && req.method === 'POST') {
        let body = '';
        req.on('data', chunk => {
            body += chunk.toString();
            if (body.length > 1e6) req.destroy(); // 1MB flood protection
        });

        req.on('end', async () => {
            try {
                const query = body ? JSON.parse(body) : {};

                if (query.source === undefined || query.destination === undefined) {
                    res.writeHead(400, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({
                        success: false,
                        error: 'Missing required parameters: "source" and "destination" are required.'
                    }));
                    return;
                }

                const result = await executeRoutingEngine(query);
                res.writeHead(result.success ? 200 : 422, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify(result, null, 2));
            } catch (err) {
                console.error('Route computation error:', err);
                res.writeHead(500, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({
                    success: false,
                    error: err.message
                }));
            }
        });
        return;
    }

    // Endpoint: GET /api/map
    if (pathname === '/api/map' && req.method === 'GET') {
        fs.readFile(MAP_FILE_PATH, 'utf8', (err, data) => {
            if (err) {
                res.writeHead(500, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ success: false, error: 'Failed to read map data file.' }));
                return;
            }
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(data);
        });
        return;
    }

    // Endpoint: GET /api/health
    if (pathname === '/api/health' && req.method === 'GET') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
            status: 'ok',
            engine: 'C Dijkstra & TSP Router (route_cli)',
            binary: BINARY_PATH,
            binary_exists: fs.existsSync(BINARY_PATH)
        }));
        return;
    }

    // Serve static files from public/
    let filePath = path.join(PUBLIC_DIR, pathname === '/' ? 'index.html' : pathname);
    
    // Security check: ensure path is within public dir
    if (!filePath.startsWith(PUBLIC_DIR)) {
        res.writeHead(403, { 'Content-Type': 'text/plain' });
        res.end('Forbidden');
        return;
    }

    fs.stat(filePath, (err, stats) => {
        if (err || !stats.isFile()) {
            res.writeHead(404, { 'Content-Type': 'text/plain' });
            res.end('404 Not Found');
            return;
        }

        const ext = path.extname(filePath).toLowerCase();
        const contentType = MIME_TYPES[ext] || 'application/octet-stream';
        res.writeHead(200, { 'Content-Type': contentType });
        fs.createReadStream(filePath).pipe(res);
    });
}

const server = http.createServer(handleRequest);

server.listen(PORT, () => {
    console.log(`=======================================================`);
    console.log(`  Smart Route Planner Backend Server running!`);
    console.log(`  Local URL:   http://localhost:${PORT}`);
    console.log(`  Route API:   POST http://localhost:${PORT}/api/route`);
    console.log(`  Map Data:    GET  http://localhost:${PORT}/api/map`);
    console.log(`  C Engine:    ${BINARY_PATH}`);
    console.log(`=======================================================`);
});
