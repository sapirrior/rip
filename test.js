// Sample JavaScript test file for `some` syntax highlighting
import { promises as fs } from 'fs';

// Constants
const DEFAULT_PORT = 8080;
const HOSTNAME = 'localhost';

/**
 * A sample HTTP Server Controller class
 */
class ServerController {
    constructor(port = DEFAULT_PORT) {
        this.port = port;
        this.active = false;
    }

    // Async method
    async start() {
        this.active = true;
        console.log(`[INFO] Server starting on http://${HOSTNAME}:${this.port}`);
        
        try {
            await this.performHealthCheck();
        } catch (error) {
            console.error("[ERROR] Health check failed:", error);
        }
    }

    performHealthCheck() {
        return new Promise((resolve, reject) => {
            const success = Math.random() > 0.1;
            if (success) {
                resolve("All systems operational");
            } else {
                reject(new Error("Database connection timed out"));
            }
        });
    }
}

// Instantiate and start
const server = new ServerController();
server.start();
