// Dev: Vite serves the lab UI on :4061 (4050 is vizbrain's) and proxies /api to the face-file server on :4060.
// Prod: the server serves the built UI from dist/ on :4060.
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  build: { outDir: "dist", emptyOutDir: true },
  // Pre-bundled up front so the first page load never strands on a 504 while Vite optimizes.
  optimizeDeps: { include: ["react", "react-dom", "react-dom/client"] },
  server: {
    port: 4061,
    strictPort: true,
    proxy: { "/api": "http://127.0.0.1:4060" },
  },
});
