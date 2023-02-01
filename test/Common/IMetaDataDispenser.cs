using System.Runtime.InteropServices;

namespace Common
{
    [Flags]
    internal enum CorOpenFlags
    {
        CopyMemory        =   0x00000002,     // Open scope with memory. Ask metadata to maintain its own copy of memory.

        ReadOnly          =   0x00000010,     // Open scope for read. Will be unable to QI for a IMetadataEmit* interface
        TakeOwnership     =   0x00000020,     // The memory was allocated with CoTaskMemAlloc and will be freed by the metadata
    }

    [ComImport]
    [Guid("809C652E-7396-11D2-9771-00A0C9B4D50C")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal unsafe interface IMetaDataDispenser
    {
        [return: MarshalAs(UnmanagedType.Interface, IidParameterIndex = 2)]
        object DefineScope(
            in Guid rclsid,
            uint dwCreateFlags,
            in Guid riid);

        [return: MarshalAs(UnmanagedType.Interface, IidParameterIndex = 2)]
        object OpenScope(
            [MarshalAs(UnmanagedType.LPWStr)] string szScope,
            uint dwCreateFlags,
            in Guid riid);

        [return:MarshalAs(UnmanagedType.Interface, IidParameterIndex = 3)]
        object OpenScopeOnMemory(
            void* pData,
            int cbData,
            CorOpenFlags dwOpenFlags,
            in Guid riid);
    }

    internal static class MetaDataDispenserOptions
    {
        public static readonly Guid MetaDataCheckDuplicatesFor = new("30FE7BE8-D7D9-11D2-9F80-00C04F79A0A3");

        public static readonly Guid MetaDataRefToDefCheck = new("DE3856F8-D7D9-11D2-9F80-00C04F79A0A3");

        public static readonly Guid MetaDataNotificationForTokenMovement = new("E5D71A4C-D7DA-11D2-9F80-00C04F79A0A3");

        public static readonly Guid MetaDataSetUpdate = new("2eee315c-d7db-11d2-9f80-00c04f79a0a3");

        public static Guid MetaDataSetENC => MetaDataSetUpdate;

        public static readonly Guid MetaDataImportOption = new("79700F36-4AAC-11d3-84C3-009027868CB1");

        public static readonly Guid MetaDataThreadSafetyOptions = new("F7559806-F266-42ea-8C63-0ADB45E8B234");

        public static readonly Guid MetaDataErrorIfEmitOutOfOrder = new("1547872D-DC03-11d2-9420-0000F8083460");

        public static readonly Guid MetaDataGenerateTCEAdapters = new("DCC9DE90-4151-11d3-88D6-00902754C43A");

        public static readonly Guid MetaDataTypeLibImportNamespace = new("F17FF889-5A63-11d3-9FF2-00C04FF7431A");

        public static readonly Guid MetaDataLinkerOptions = new("47E099B6-AE7C-4797-8317-B48AA645B8F9");

        public static readonly Guid MetaDataRuntimeVersion = new("47E099B7-AE7C-4797-8317-B48AA645B8F9");

        public static readonly Guid MetaDataMergerOptions = new("132D3A6E-B35D-464e-951A-42EFB9FB6601");

        public static readonly Guid MetaDataPreserveLocalRefs = new("a55c0354-e91b-468b-8648-7cc31035d533");
    }

    public enum CorSetENC : uint
    {
        MDSetENCOn = 0x1,
        MDSetENCOff = 0x2,
        MDUpdateENC = 0x1,
        MDUpdateFull = 0x2,
        MDUpdateExtension = 0x3,
        MDUpdateIncremental = 0x4,
        MDUpdateDelta = 0x5,
        MDUpdateMask = 0x7
    }

    [ComImport]
    [Guid("31BCFCE2-DAFB-11D2-9F81-00C04F79A0A3")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal unsafe interface IMetaDataDispenserEx : IMetaDataDispenser
    {
        [return: MarshalAs(UnmanagedType.Interface, IidParameterIndex = 2)]
        new object DefineScope(
            in Guid rclsid,
            uint dwCreateFlags,
            in Guid riid);

        [return: MarshalAs(UnmanagedType.Interface, IidParameterIndex = 2)]
        new object OpenScope(
            [MarshalAs(UnmanagedType.LPWStr)] string szScope,
            uint dwCreateFlags,
            in Guid riid);

        [return: MarshalAs(UnmanagedType.Interface, IidParameterIndex = 3)]
        new object OpenScopeOnMemory(
            void* pData,
            int cbData,
            CorOpenFlags dwOpenFlags,
            in Guid riid);

        [PreserveSig]
        int SetOption(
            in Guid optionid,
            [MarshalAs(UnmanagedType.Struct)] object value);

        [PreserveSig]
        int GetOption(
            in Guid optionid,
            [MarshalAs(UnmanagedType.Struct)] out object value);

        [PreserveSig]
        int OpenScopeOnITypeInfo();

        [PreserveSig]
        int GetCORSystemDirectory();

        [PreserveSig]
        int FindAssembly();

        [PreserveSig]
        int FindAssemblyModule();
    }
}
