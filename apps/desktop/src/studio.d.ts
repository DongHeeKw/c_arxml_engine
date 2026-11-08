export {};

declare global {
  interface Window {
    studio: {
      openDbcFiles(): Promise<{ name: string; text: string; sourceUri: string }[]>;
      pathForFile(file: File): string;
      inspectProject(): Promise<{ root: string; files: unknown[]; eclipseProject: boolean; issues: { code: string; path: string }[] } | null>;
      request<T = unknown>(method: string, params?: Record<string, unknown>): Promise<T>;
    };
  }
}
