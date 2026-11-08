export {};

declare global {
  interface Window {
    studio: {
      request<T = unknown>(method: string, params?: Record<string, unknown>): Promise<T>;
    };
  }
}
