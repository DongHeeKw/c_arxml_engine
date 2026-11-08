export {};

declare global {
  interface Window {
    studio: {
      openDbcFiles(): Promise<{ name: string; text: string }[]>;
      request<T = unknown>(method: string, params?: Record<string, unknown>): Promise<T>;
    };
  }
}
