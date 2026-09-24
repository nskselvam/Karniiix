import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  images: {
    remotePatterns: [
      { protocol: "http", hostname: "localhost", port: "4001" },
      { protocol: "http", hostname: "127.0.0.1", port: "4001" },
    ],
  },
};

export default nextConfig;
