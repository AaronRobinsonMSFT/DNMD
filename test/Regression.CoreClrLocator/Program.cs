// Output the location of CoreCLR so we can locate it in the tests.
Console.WriteLine(Path.Combine(Path.GetDirectoryName(typeof(object).Assembly.Location!)!, OperatingSystem.IsWindows() ? "coreclr.dll" : "libcoreclr.so"));
