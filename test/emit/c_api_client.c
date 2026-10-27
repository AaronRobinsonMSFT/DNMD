#include <dnmd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool check_user_string(mdhandle_t handle, mduserstringcursor_t start)
{
    mduserstringcursor_t cursor = start;
    mduserstring_t value;
    uint32_t offset;
    return md_walk_user_string_heap(handle, &cursor, &value, &offset)
        && offset == (uint32_t)start
        && value.str_bytes == sizeof(u"Retained") - sizeof(char16_t) + 1
        && value.final_byte == 0
        && memcmp(value.str, u"Retained", sizeof(u"Retained") - sizeof(char16_t)) == 0
        && !md_walk_user_string_heap(handle, &cursor, &value, &offset);
}

int main(int argc, char** argv)
{
    int result = 1;
    mdhandle_t handle = md_create_new_handle();
    mdhandle_t loaded = NULL;
    uint8_t* data = NULL;
    FILE* file = NULL;
    size_t size = 0;
    mduserstringcursor_t cursor = 0;
    mduserstring_t value;
    uint32_t offset;
    if (handle == NULL || !md_validate(handle)
        || md_validate(NULL) || md_apply_delta(handle, NULL) || md_dump_tables(NULL, 0)
        || md_walk_user_string_heap(NULL, &cursor, &value, &offset))
        goto cleanup;
    mduserstringcursor_t string_offset = md_add_userstring_to_heap(handle, u"Retained");
    if (string_offset <= 0 || !check_user_string(handle, string_offset)
        || !md_walk_user_string_heap(handle, &cursor, &value, &offset)
        || offset != 0 || value.str_bytes != 0 || cursor != string_offset)
        goto cleanup;
    if (md_write_to_buffer(handle, NULL, &size) || size == 0)
        goto cleanup;
    data = calloc(size, 1);
    if (data == NULL || !md_write_to_buffer(handle, data, &size)
        || !md_create_handle(data, size, &loaded) || !md_validate(loaded)
        || !check_user_string(loaded, string_offset)
        || strcmp(md_get_version_string(loaded), "v4.0.30319") != 0)
        goto cleanup;
    if (argc > 1)
    {
        file = fopen(argv[1], "wb");
        if (file == NULL || fwrite(data, 1, size, file) != size)
            goto cleanup;
        fclose(file);
        file = NULL;
    }
    if (!md_dump_tables(handle, argc > 2 ? -1 : mdtid_Module))
        goto cleanup;
    result = 0;
cleanup:
    if (file != NULL)
        fclose(file);
    md_destroy_handle(loaded);
    md_destroy_handle(handle);
    free(data);
    return result;
}
