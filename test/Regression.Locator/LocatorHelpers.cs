using System.Runtime.InteropServices;
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
}
