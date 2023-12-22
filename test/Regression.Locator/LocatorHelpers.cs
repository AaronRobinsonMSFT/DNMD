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

    [UnmanagedCallersOnly(EntryPoint = "GetImageAndDeltas")]
    public static void GetImageAndDeltas(byte** image, uint* imageLen, uint* deltaCount, byte*** deltas, uint** deltaLengths)
    {
        var asm = DeltaImageBuilder.CreateAssembly();
        *imageLen = (uint)asm.BaseImage.Length;
        *image = (byte*)NativeMemory.Alloc(*imageLen);
        asm.BaseImage.CopyTo(new Span<byte>(*image, (int)*imageLen));

        *deltaCount = (uint)asm.MetadataDeltas.Length;
        *deltas = (byte**)NativeMemory.Alloc(*deltaCount * (uint)sizeof(byte*));
        *deltaLengths = (uint*)NativeMemory.Alloc(*deltaCount * sizeof(uint));

        for (int i = 0; i < asm.MetadataDeltas.Length; i++)
        {
            (*deltas)[i] = (byte*)NativeMemory.Alloc((uint)asm.MetadataDeltas[i].Length);
            asm.MetadataDeltas[i].CopyTo(new Span<byte>((*deltas)[i], asm.MetadataDeltas[i].Length));
            (*deltaLengths)[i] = (uint)asm.MetadataDeltas[i].Length;
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "FreeImageAndDeltas")]
    public static void FreeImageAndDeltas(byte* image, uint deltaCount, byte** deltas, uint* deltaLengths)
    {
        NativeMemory.Free(image);
        for (int i = 0; i < deltaCount; i++)
        {
            NativeMemory.Free(deltas[i]);
        }
        NativeMemory.Free(deltas);
        NativeMemory.Free(deltaLengths);
    }
}
