#include "internal.hpp"

// Initialize the minimal set of tables required for a valid metadata image.
// Every image must have a row in the Module table
// for module identity information
// and a row in the TypeDef table for the global type.
bool initialize_minimal_table_rows(mdcxt_t* context)
{
    using namespace dnmd;
    using namespace dnmd::columns;

    // Add the Module row for module identity.
    auto module = added_row<mdtid_Module>::append(context);
    if (!module)
        return false;

    row_writer module_writer{module.value()};
    mdguid_t empty{};
    // Set the Generation to 0.
    if (!module_writer.write(Module_Generation, 0)
        // Use the 0 index to specify the NULL GUIDs for the image.
        || !module_writer.write(Module_Mvid, empty)
        || !module_writer.write(Module_EncBaseId, empty)
        || !module_writer.write(Module_EncId, empty)
        || !module_writer.write(Module_Name, ""))
        return false;
    // Mark that we're done adding the Module row.
    module.commit();

    // Add a row for the global <Module> type.
    auto global_type = added_row<mdtid_TypeDef>::append(context);
    if (!global_type)
        return false;

    row_writer type_writer{global_type.value()};
    // The guard marks the TypeDef row complete when this scope exits.
    return type_writer.write(TypeDef_Flags, 0)
        && type_writer.write(TypeDef_TypeName, "<Module>") // Defined in ECMA-335 II.10.8.
        && type_writer.write(TypeDef_TypeNamespace, "")
        && type_writer.write(TypeDef_Extends, CreateTokenType(mdtid_TypeDef));
}
