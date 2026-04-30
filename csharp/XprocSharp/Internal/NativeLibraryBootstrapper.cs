using System.Reflection;
using System.Runtime.InteropServices;

namespace XprocSharp.Internal;

internal static class NativeLibraryBootstrapper
{
    private static bool s_registered;
    private static readonly object Sync = new();

    public static void EnsureRegistered()
    {
        lock (Sync)
        {
            if (s_registered)
            {
                return;
            }

            NativeLibrary.SetDllImportResolver(typeof(NativeMethods).Assembly, Resolve);
            s_registered = true;
        }
    }

    private static IntPtr Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (!string.Equals(libraryName, NativeMethods.LibraryName, StringComparison.Ordinal))
        {
            return IntPtr.Zero;
        }

        foreach (var candidate in CandidatePaths())
        {
            if (NativeLibrary.TryLoad(candidate, out var handle))
            {
                return handle;
            }
        }

        return IntPtr.Zero;
    }

    private static IEnumerable<string> CandidatePaths()
    {
        var fileName = RuntimeInformation.IsOSPlatform(OSPlatform.Windows)
            ? "xproc_c.dll"
            : RuntimeInformation.IsOSPlatform(OSPlatform.OSX)
                ? "libxproc_c.dylib"
                : "libxproc_c.so";

        var overrideDir = Environment.GetEnvironmentVariable("XPROC_CSHARP_NATIVE_DIR");
        if (!string.IsNullOrWhiteSpace(overrideDir))
        {
            yield return Path.Combine(overrideDir, fileName);
        }

        yield return Path.Combine(AppContext.BaseDirectory, fileName);
        yield return Path.Combine(AppContext.BaseDirectory, "runtimes", RuntimeInformation.RuntimeIdentifier, "native", fileName);

        var current = new DirectoryInfo(AppContext.BaseDirectory);
        for (var depth = 0; depth < 8 && current is not null; depth += 1)
        {
            yield return Path.Combine(current.FullName, "build", "capi", "Debug", fileName);
            yield return Path.Combine(current.FullName, "build", "capi", "Release", fileName);
            yield return Path.Combine(current.FullName, "build", "capi", "RelWithDebInfo", fileName);
            yield return Path.Combine(current.FullName, "build", "capi", "MinSizeRel", fileName);
            current = current.Parent;
        }
    }
}
