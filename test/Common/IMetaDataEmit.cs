using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;

namespace Common
{
    public enum CorSaveSize : uint
    {
        Accurate = 0x0000,
        Quick = 0x0001,
        DiscardTransientCAs = 0x0002
    }

    [ComImport]
    [Guid("7DAC8207-D3AE-4c75-9B67-92801A497D44")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IMetaDataImport { }

    [ComImport]
    [Guid("BA3FEE4C-ECB9-4e41-83B7-183FA41CD859")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IMetaDataEmit
    {
        [PreserveSig]
        int SetModuleProps(
            [MarshalAs(UnmanagedType.LPWStr)] string szName);
        
        [PreserveSig]
        int Save(
            [MarshalAs(UnmanagedType.LPWStr)] string szFile,
            [MarshalAs(UnmanagedType.U4)] uint dwSaveFlags);
        
        [PreserveSig]
        int SaveToStream(
            [MarshalAs(UnmanagedType.Interface)] IStream pIStream,
            [MarshalAs(UnmanagedType.U4)] uint dwSaveFlags);
        
        [PreserveSig]
        int GetSaveSize(
            [MarshalAs(UnmanagedType.U4)] CorSaveSize fSave,
            [MarshalAs(UnmanagedType.U4)] out uint pdwSaveSize);

        [PreserveSig]
        int DefineTypeDef(
            [MarshalAs(UnmanagedType.LPWStr)] string szTypeDef,
            [MarshalAs(UnmanagedType.U4)] uint dwTypeDefFlags,
            [MarshalAs(UnmanagedType.U4)] uint tkExtends,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 4)] uint[] rtkImplements,
            [MarshalAs(UnmanagedType.U4)] uint ctkImplements,
            [MarshalAs(UnmanagedType.U4)] out uint ptd);
        
        [PreserveSig]
        int DefineNestedType(
            [MarshalAs(UnmanagedType.LPWStr)] string szTypeDef,
            [MarshalAs(UnmanagedType.U4)] uint dwTypeDefFlags,
            [MarshalAs(UnmanagedType.U4)] uint tkExtends,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 4)] uint[] rtkImplements,
            [MarshalAs(UnmanagedType.U4)] uint ctkImplements,
            [MarshalAs(UnmanagedType.U4)] uint tkEncloser,
            [MarshalAs(UnmanagedType.U4)] out uint ptd);
        
        [PreserveSig]
        int SetHandler(
            [MarshalAs(UnmanagedType.Interface)] object pUnk);
        
        [PreserveSig]
        int DefineMethod(
            [MarshalAs(UnmanagedType.U4)] uint td,
            [MarshalAs(UnmanagedType.LPWStr)] string szName,
            [MarshalAs(UnmanagedType.U4)] uint dwMethodFlags,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 4)] byte[] pvSigBlob,
            [MarshalAs(UnmanagedType.U4)] uint cbSigBlob,
            [MarshalAs(UnmanagedType.U4)] uint ulCodeRVA,
            [MarshalAs(UnmanagedType.U4)] uint dwImplFlags,
            [MarshalAs(UnmanagedType.U4)] out uint pmd);
        
        [PreserveSig]
        int DefineMethodImpl(
            [MarshalAs(UnmanagedType.U4)] uint td,
            [MarshalAs(UnmanagedType.U4)] uint tkBody,
            [MarshalAs(UnmanagedType.U4)] uint tkDecl);
        
        [PreserveSig]
        int DefineTypeRefByName(
            [MarshalAs(UnmanagedType.U4)] uint tkResolutionScope,
            [MarshalAs(UnmanagedType.LPWStr)] string szName,
            [MarshalAs(UnmanagedType.U4)] out uint ptr);
        
        [PreserveSig]
        int DefineImportType(
            [MarshalAs(UnmanagedType.Interface)] object pAssemImport,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 2)] byte[] pbHashValue,
            [MarshalAs(UnmanagedType.U4)] uint cbHashValue,
            [MarshalAs(UnmanagedType.Interface)] object pImport,
            [MarshalAs(UnmanagedType.U4)] uint tdImport,
            [MarshalAs(UnmanagedType.Interface)] object pAssemEmit,
            [MarshalAs(UnmanagedType.U4)] out uint ptr);
        
        [PreserveSig]
        int DefineMemberRef(
            [MarshalAs(UnmanagedType.U4)] uint tkImport,
            [MarshalAs(UnmanagedType.LPWStr)] string szName,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 3)] byte[] pvSigBlob,
            [MarshalAs(UnmanagedType.U4)] uint cbSigBlob,
            [MarshalAs(UnmanagedType.U4)] out uint pmr);
        
        [PreserveSig]
        int DefineImportMember(
            [MarshalAs(UnmanagedType.Interface)] object pAssemImport,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 2)] byte[] pbHashValue,
            [MarshalAs(UnmanagedType.U4)] uint cbHashValue,
            [MarshalAs(UnmanagedType.Interface)] object pImport,
            [MarshalAs(UnmanagedType.U4)] uint mbMember,
            [MarshalAs(UnmanagedType.Interface)] object pAssemEmit,
            [MarshalAs(UnmanagedType.U4)] uint tkParent,
            [MarshalAs(UnmanagedType.U4)] out uint pmr);
        
        [PreserveSig]
        int DefineEvent(
            [MarshalAs(UnmanagedType.U4)] uint td,
            [MarshalAs(UnmanagedType.LPWStr)] string szEvent,
            [MarshalAs(UnmanagedType.U4)] uint dwEventFlags,
            [MarshalAs(UnmanagedType.U4)] uint tkEventType,
            [MarshalAs(UnmanagedType.U4)] uint mdAddOn,
            [MarshalAs(UnmanagedType.U4)] uint mdRemoveOn,
            [MarshalAs(UnmanagedType.U4)] uint mdFire,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 8)] uint[] rmdOtherMethod,
            [MarshalAs(UnmanagedType.U4)] uint cOtherMethod,
            [MarshalAs(UnmanagedType.U4)] out uint pmdEvent);
        
        [PreserveSig]
        int SetClassLayout(
            [MarshalAs(UnmanagedType.U4)] uint td,
            [MarshalAs(UnmanagedType.U2)] ushort dwPackSize,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 3)] uint[] rFieldMarshal,
            [MarshalAs(UnmanagedType.U4)] uint ulClassSize,
            [MarshalAs(UnmanagedType.U4)] uint cFieldMarshal,
            [MarshalAs(UnmanagedType.U4)] uint ulClassIfaceSize);
        
        [PreserveSig]
        int DeleteClassLayout(
            [MarshalAs(UnmanagedType.U4)] uint td);
        
        [PreserveSig]
        int SetFieldMarshal(
            [MarshalAs(UnmanagedType.U4)] uint tk,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 2)] byte[] pvNativeType,
            [MarshalAs(UnmanagedType.U4)] uint cbNativeType);
        
        [PreserveSig]
        int DeleteFieldMarshal(
            [MarshalAs(UnmanagedType.U4)] uint tk);
        
        [PreserveSig]
        int DefinePermissionSet(
            [MarshalAs(UnmanagedType.U4)] uint tk,
            [MarshalAs(UnmanagedType.U4)] uint dwAction,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 3)] byte[] pvPermission,
            [MarshalAs(UnmanagedType.U4)] uint cbPermission,
            [MarshalAs(UnmanagedType.U4)] out uint ppm);
        
        [PreserveSig]
        int SetRVA(
            [MarshalAs(UnmanagedType.U4)] uint md,
            [MarshalAs(UnmanagedType.U4)] uint ulRVA);
        
        [PreserveSig]
        int GetTokenFromSig(
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 1)] byte[] pvSig,
            [MarshalAs(UnmanagedType.U4)] uint cbSig,
            [MarshalAs(UnmanagedType.U4)] out uint pmsig);
        
        [PreserveSig]
        int DefineModuleRef(
            [MarshalAs(UnmanagedType.LPWStr)] string szName,
            [MarshalAs(UnmanagedType.Interface)] object pAssem,
            [MarshalAs(UnmanagedType.U4)] out uint pmur);
        
        [PreserveSig]
        int SetParent(
            [MarshalAs(UnmanagedType.U4)] uint mr,
            [MarshalAs(UnmanagedType.U4)] uint tk);
        
        [PreserveSig]
        int GetTokenFromTypeSpec(
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 1)] byte[] pvSig,
            [MarshalAs(UnmanagedType.U4)] uint cbSig,
            [MarshalAs(UnmanagedType.U4)] out uint typespec);
        
        [PreserveSig]
        int SaveToMemory(
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 1)] byte[] pbData,
            [MarshalAs(UnmanagedType.U4)] uint cbData);
        
        [PreserveSig]
        int DefineUserString(
            [MarshalAs(UnmanagedType.LPWStr)] string szString,
            [MarshalAs(UnmanagedType.U4)] uint cchString,
            [MarshalAs(UnmanagedType.U4)] out uint stk);
        
        [PreserveSig]
        int DeleteToken(
            [MarshalAs(UnmanagedType.U4)] uint tkObj);
        
        [PreserveSig]
        int SetMethodProps(
            [MarshalAs(UnmanagedType.U4)] uint md,
            [MarshalAs(UnmanagedType.U4)] uint dwMethodFlags,
            [MarshalAs(UnmanagedType.U4)] uint ulCodeRVA,
            [MarshalAs(UnmanagedType.U4)] uint dwImplFlags);
        
        [PreserveSig]
        int SetTypeDefProps(
            [MarshalAs(UnmanagedType.U4)] uint td,
            [MarshalAs(UnmanagedType.U4)] uint dwTypeDefFlags,
            [MarshalAs(UnmanagedType.U4)] uint tkExtends,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 4)] uint[] rtkImplements,
            [MarshalAs(UnmanagedType.U4)] uint ctkImplements);
        
        [PreserveSig]
        int SetEventProps(
            [MarshalAs(UnmanagedType.U4)] uint ev,
            [MarshalAs(UnmanagedType.U4)] uint dwEventFlags,
            [MarshalAs(UnmanagedType.U4)] uint tkEventType,
            [MarshalAs(UnmanagedType.U4)] uint mdAddOn,
            [MarshalAs(UnmanagedType.U4)] uint mdRemoveOn,
            [MarshalAs(UnmanagedType.U4)] uint mdFire,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 7)] uint[] rmdOtherMethod,
            [MarshalAs(UnmanagedType.U4)] uint cOtherMethod);
        
        [PreserveSig]
        int SetPermissionSetProps(
            [MarshalAs(UnmanagedType.U4)] uint tk,
            [MarshalAs(UnmanagedType.U4)] uint dwAction,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 3)] byte[] pvPermission,
            [MarshalAs(UnmanagedType.U4)] uint cbPermission,
            [MarshalAs(UnmanagedType.U4)] out uint ppm);
        
        [PreserveSig]
        int DefinePinvokeMap(
            [MarshalAs(UnmanagedType.U4)] uint tk,
            [MarshalAs(UnmanagedType.U4)] uint dwMappingFlags,
            [MarshalAs(UnmanagedType.LPWStr)] string szImportName,
            [MarshalAs(UnmanagedType.U4)] uint mrImportDLL);
        
        [PreserveSig]
        int SetPinvokeMap(
            [MarshalAs(UnmanagedType.U4)] uint tk,
            [MarshalAs(UnmanagedType.U4)] uint dwMappingFlags,
            [MarshalAs(UnmanagedType.LPWStr)] string szImportName,
            [MarshalAs(UnmanagedType.U4)] uint mrImportDLL);
        
        [PreserveSig]
        int DeletePinvokeMap(
            [MarshalAs(UnmanagedType.U4)] uint tk);
        
        [PreserveSig]
        int DefineCustomAttribute(
            [MarshalAs(UnmanagedType.U4)] uint tkObj,
            [MarshalAs(UnmanagedType.U4)] uint tkType,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 3)] byte[] pvData,
            [MarshalAs(UnmanagedType.U4)] uint cbData,
            [MarshalAs(UnmanagedType.U4)] out uint pcv);
        
        [PreserveSig]
        int SetCustomAttributeValue(
            [MarshalAs(UnmanagedType.U4)] uint pcv,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 2)] byte[] pvData,
            [MarshalAs(UnmanagedType.U4)] uint cbData);

        [PreserveSig]
        int DefineField(
            [MarshalAs(UnmanagedType.U4)] uint td,
            [MarshalAs(UnmanagedType.LPWStr)] string szName,
            [MarshalAs(UnmanagedType.U4)] uint dwFieldFlags,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 4)] byte[] pvSigBlob,
            [MarshalAs(UnmanagedType.U4)] uint cbSigBlob,
            [MarshalAs(UnmanagedType.U4)] uint dwCPlusTypeFlag,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 6)] byte[] pValue,
            [MarshalAs(UnmanagedType.U4)] uint cchValue,
            [MarshalAs(UnmanagedType.U4)] out uint pmd);
        
        [PreserveSig]
        int DefineProperty(
            [MarshalAs(UnmanagedType.U4)] uint td,
            [MarshalAs(UnmanagedType.LPWStr)] string szProperty,
            [MarshalAs(UnmanagedType.U4)] uint dwPropFlags,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 4)] byte[] pvSig,
            [MarshalAs(UnmanagedType.U4)] uint cbSig,
            [MarshalAs(UnmanagedType.U4)] uint dwCPlusTypeFlag,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 6)] byte[] pValue,
            [MarshalAs(UnmanagedType.U4)] uint cchValue,
            [MarshalAs(UnmanagedType.U4)] uint mdSetter,
            [MarshalAs(UnmanagedType.U4)] uint mdGetter,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 10)] uint[] rmdOtherMethod,
            [MarshalAs(UnmanagedType.U4)] uint cOtherMethod,
            [MarshalAs(UnmanagedType.U4)] out uint pmdProp);
        
        [PreserveSig]
        int DefineParam(
            [MarshalAs(UnmanagedType.U4)] uint md,
            [MarshalAs(UnmanagedType.U4)] uint ulParamSeq,
            [MarshalAs(UnmanagedType.LPWStr)] string szName,
            [MarshalAs(UnmanagedType.U4)] uint dwParamFlags,
            [MarshalAs(UnmanagedType.U4)] uint dwCPlusTypeFlag,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 5)] byte[] pValue,
            [MarshalAs(UnmanagedType.U4)] uint cchValue,
            [MarshalAs(UnmanagedType.U4)] out uint ppd);
        
        [PreserveSig]
        int SetFieldProps(
            [MarshalAs(UnmanagedType.U4)] uint fd,
            [MarshalAs(UnmanagedType.U4)] uint dwFieldFlags,
            [MarshalAs(UnmanagedType.U4)] uint dwCPlusTypeFlag,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 3)] byte[] pValue,
            [MarshalAs(UnmanagedType.U4)] uint cchValue);
        
        [PreserveSig]
        int SetPropertyProps(
            [MarshalAs(UnmanagedType.U4)] uint pr,
            [MarshalAs(UnmanagedType.U4)] uint dwPropFlags,
            [MarshalAs(UnmanagedType.U4)] uint dwCPlusTypeFlag,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 3)] byte[] pValue,
            [MarshalAs(UnmanagedType.U4)] uint cchValue,
            [MarshalAs(UnmanagedType.U4)] uint mdSetter,
            [MarshalAs(UnmanagedType.U4)] uint mdGetter,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 7)] uint[] rmdOtherMethod,
            [MarshalAs(UnmanagedType.U4)] uint cOtherMethod);
        
        [PreserveSig]
        int SetParamProps(
            [MarshalAs(UnmanagedType.U4)] uint pd,
            [MarshalAs(UnmanagedType.LPWStr)] string szName,
            [MarshalAs(UnmanagedType.U4)] uint dwParamFlags,
            [MarshalAs(UnmanagedType.U4)] uint dwCPlusTypeFlag,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 4)] byte[] pValue,
            [MarshalAs(UnmanagedType.U4)] uint cchValue);
        
        [PreserveSig]
        int DefineSecurityAttributeSet(
            [MarshalAs(UnmanagedType.U4)] uint tkObj,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 2)] byte[] rSecAttrs,
            [MarshalAs(UnmanagedType.U4)] uint cSecAttrs,
            [MarshalAs(UnmanagedType.U4)] out uint pcv);
        
        [PreserveSig]
        int ApplyEditAndContinue(
            [MarshalAs(UnmanagedType.Interface)] object pImport);
        
        [PreserveSig]
        int TranslateSigWithScope(
            [MarshalAs(UnmanagedType.Interface)] object pAssemImport,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 2)] byte[] pbHashValue,
            [MarshalAs(UnmanagedType.U4)] uint cbHashValue,
            [MarshalAs(UnmanagedType.U4)] uint ulImportToken,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 5)] byte[] pbSigBlob,
            [MarshalAs(UnmanagedType.U4)] uint cbSigBlob,
            [MarshalAs(UnmanagedType.Interface)] IMetaDataImport pAssemImport2,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 8)] byte[] pbHashValue2,
            [MarshalAs(UnmanagedType.U4)] uint cbHashValue2,
            [MarshalAs(UnmanagedType.U4)] uint ulImportToken2,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 11)] byte[] pbSig,
            [MarshalAs(UnmanagedType.U4)] uint cbSigMax,
            [MarshalAs(UnmanagedType.U4)] out uint pcbSig);
        
        [PreserveSig]
        int SetMethodImplFlags(
            [MarshalAs(UnmanagedType.U4)] uint md,
            [MarshalAs(UnmanagedType.U4)] uint dwImplFlags);
        
        [PreserveSig]
        int SetFieldRVA(
            [MarshalAs(UnmanagedType.U4)] uint fd,
            [MarshalAs(UnmanagedType.U4)] uint ulRVA);
        
        [PreserveSig]
        int Merge(
            [MarshalAs(UnmanagedType.Interface)] object pImport,
            [MarshalAs(UnmanagedType.U4)] uint tkMergeTarget,
            [MarshalAs(UnmanagedType.Interface)] IMetaDataImport pHostImport,
            [MarshalAs(UnmanagedType.Interface)] object pHandler);
        
        [PreserveSig]
        int MergeEnd();
    }
}