#include <stdio.h>
#include <stdlib.h>
#include "ss_mpi_sys.h"
#include "svp_npu/svp_acl.h"
#include "svp_npu/svp_acl_mdl.h"

int main(int argc, char** argv) {
    if (argc < 2) return -1;
    ss_mpi_sys_init();
    
    td_u32 model_id = 0;
    svp_acl_mdl_desc *desc = NULL;
    td_s32 ret = svp_acl_mdl_load_from_file(argv[1], &model_id);
    if (ret != 0) {
        printf("load failed\n");
        return -1;
    }
    desc = svp_acl_mdl_create_desc();
    svp_acl_mdl_get_desc(desc, model_id);
    
    size_t in_num = svp_acl_mdl_get_num_inputs(desc);
    printf("Inputs: %zu\n", in_num);
    for (size_t i = 0; i < in_num; i++) {
        svp_acl_mdl_io_dims dims;
        svp_acl_mdl_get_input_dims(desc, i, &dims);
        printf("  Input %zu: [", i);
        for(size_t j=0; j<dims.dim_count; j++) printf("%ld ", dims.dims[j]);
        printf("]\n");
    }
    
    size_t out_num = svp_acl_mdl_get_num_outputs(desc);
    printf("Outputs: %zu\n", out_num);
    for (size_t i = 0; i < out_num; i++) {
        svp_acl_mdl_io_dims dims;
        svp_acl_mdl_get_output_dims(desc, i, &dims);
        printf("  Output %zu: [", i);
        for(size_t j=0; j<dims.dim_count; j++) printf("%ld ", dims.dims[j]);
        printf("]\n");
    }
    
    return 0;
}
