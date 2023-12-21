using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using Microsoft.Win32;
namespace Regression.Locator;

public static unsafe class LocatorHelpers
{
    [UnmanagedCallersOnly(EntryPoint = "GetCoreClrPath")]
    public static byte* GetCoreClrPath()
    {
        string path = Path.Combine(Path.GetDirectoryName(typeof(object).Assembly.Location!)!, OperatingSystem.IsWindows() ? "coreclr.dll" : "libcoreclr.so");
        return (byte*)Marshal.StringToCoTaskMemUTF8(path);
    }
    
    [UnmanagedCallersOnly(EntryPoint = "GetRegressionTargetAssemblyPath")]
    public static byte* GetCoreLibPath()
    {
        string path = Path.Combine(Path.GetDirectoryName(typeof(LocatorHelpers).Assembly.Location!)!, "Regression.TargetAssembly.dll");
        return (byte*)Marshal.StringToCoTaskMemUTF8(path);
    }

    [SupportedOSPlatform("windows")]
    [UnmanagedCallersOnly(EntryPoint = "GetFrameworkPath")]
    public static byte* GetFrameworkPath([DNNE.C99Type("char const*")]sbyte* version)
    {
        if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            return null;
        }

        using var key = Registry.LocalMachine.OpenSubKey("SOFTWARE\\Microsoft\\.NETFramework")!;
        string path = Path.Combine((string)key.GetValue("InstallRoot")!, new string(version));
        return (byte*)Marshal.StringToCoTaskMemUTF8(path);
    }
}
