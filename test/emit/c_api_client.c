#include <dnmd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
    int result = 1;
    mdhandle_t handle = md_create_new_handle();
    mdhandle_t loaded = NULL;
    uint8_t* data = NULL;
    FILE* file = NULL;
    size_t size = 0;
    if (handle == NULL || !md_validate(handle)
        || md_validate(NULL) || md_apply_delta(handle, NULL) || md_dump_tables(NULL, 0))
        goto cleanup;
    if (md_write_to_buffer(handle, NULL, &size) || size == 0)
        goto cleanup;
    data = calloc(size, 1);
    if (data == NULL || !md_write_to_buffer(handle, data, &size)
        || !md_create_handle(data, size, &loaded) || !md_validate(loaded)
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
