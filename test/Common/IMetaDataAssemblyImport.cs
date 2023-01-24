using System.Runtime.InteropServices;

namespace Common
{
    [ComImport]
    [Guid("EE62470B-E94B-424E-9B7C-2F00C9249F93")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IMetaDataAssemblyImport
    {
        [PreserveSig]
        int GetAssemblyProps(uint mdAssembly, out IntPtr ppbPublicKey, out uint pcbPublicKey, out uint pulHashAlgId, [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 5)]char[] szName, int cchName, out int pchName, ref ASSEMBLYMETADATA pMetaData, out uint pdwAssemblyFlags);

        [PreserveSig]
        int GetAssemblyRefProps(uint mdAssemblyRef, out IntPtr ppbPublicKeyOrToken, out uint pcbPublicKeyOrToken, [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 4)]char[] szName, int cchName, out int pchName, ref ASSEMBLYMETADATA pMetaData, out nint ppbHashValue, out uint pcbHashValue, out uint pdwAssemblyRefFlags);

        [PreserveSig]
        int GetFileProps(uint mdFile, [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 2)]char[] szName, int cchName, out int pchName, out IntPtr ppbHashValue, out uint pcbHashValue, out uint pdwFileFlags);

        [PreserveSig]
        int GetExportedTypeProps(uint mdExportedType, [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 2)]char[] szName, int cchName, out int pchName, out uint ptkImplementation, out uint ptkTypeDef, out uint pdwExportedTypeFlags);

        [PreserveSig]
        int GetManifestResourceProps(uint mdManifestResource, [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 2)]char[] szName, int cchName, out int pchName, out uint ptkImplementation, out uint pdwOffset, out uint pdwResourceFlags);

        [PreserveSig]
        int EnumAssemblyRefs(ref IntPtr phEnum, [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 2)]uint[] rAssemblyRefs, int cMax, out uint pcTokens);

        [PreserveSig]
        int EnumFiles(ref IntPtr phEnum, [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 2)]uint[] rFiles, int cMax, out uint pcTokens);

        [PreserveSig]
        int EnumExportedTypes(ref IntPtr phEnum, [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 2)]uint[] rExportedTypes, int cMax, out uint pcTokens);

        [PreserveSig]
        int EnumManifestResources(ref IntPtr phEnum, [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 2)]uint[] rManifestResources, int cMax, out uint pcTokens);

        [PreserveSig]
        int GetAssemblyFromScope(out uint pmdAsm);

        [PreserveSig]
        int FindExportedTypeByName([MarshalAs(UnmanagedType.LPWStr)]string szName, uint tkImplementation, out uint pmdExportedType);

        [PreserveSig]
        int FindManifestResourceByName([MarshalAs(UnmanagedType.LPWStr)]string szName, out uint ptkManifestResource);

        [PreserveSig]
        void CloseEnum(IntPtr hEnum);

        [PreserveSig]
        int FindAssembliesByName([MarshalAs(UnmanagedType.LPWStr)]string szAppBase, [MarshalAs(UnmanagedType.LPWStr)]string szPrivateBin, [MarshalAs(UnmanagedType.LPWStr)]string szAssemblyName, [MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.IUnknown)] object[] ppIUnk, uint cMax, out uint pcAssemblies);
    }

    public struct OSINFO
    {
        public uint dwOSPlatformId;
        public uint dwOSMajorVersion;
        public uint dwOSMinorVersion;
    }

    public struct ASSEMBLYMETADATA
    {
        public ushort usMajorVersion;
        public ushort usMinorVersion;
        public ushort usBuildNumber;
        public ushort usRevisionNumber;
        public IntPtr szLocale;
        public uint cbLocale;
        public IntPtr rProcessor;
        public uint ulProcessor;
        public IntPtr rOS;
        public uint ulOS;
    }
}
